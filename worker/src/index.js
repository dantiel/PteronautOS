// PteronautOS Cloud Build — Cloudflare Worker backend.
//
// Holds a GitHub token server-side (never in the browser) and drives the
// `cloud-build.yml` workflow: trigger → poll → extract the .bin from the
// GitHub artifact zip → serve raw bytes for esptool-js Web Serial flashing.
//
// This worker is API-ONLY. The flasher webapp lives on GitHub Pages
// (dantiel.github.io/PteronautOS/flasher/) and calls these endpoints
// cross-origin via the baked-in DEFAULT_API_BASE. No static assets here —
// the worker must not duplicate the docs site.

import { unzipSync } from "fflate";

const WORKFLOW_NAME = "PteronautOS Cloud Build";
const ARTIFACT_NAME = "pteronautos-firmware";

const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, Authorization",
};

function json(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json", ...CORS },
  });
}

function githubFetch(env, path, init = {}) {
  const url = path.startsWith("https://")
    ? path
    : `https://api.github.com${path}`;
  return fetch(url, {
    ...init,
    headers: {
      Authorization: `Bearer ${env.GITHUB_TOKEN}`,
      Accept: "application/vnd.github+json",
      "User-Agent": "pteronautos-build-worker",
      ...(init.headers || {}),
    },
  });
}

// workflow_dispatch returns 204 with no run id — locate the freshly queued
// run by polling the workflow's run list until it appears.
async function findNewestRun(env, dispatchTime) {
  for (let i = 0; i < 30; i++) {
    const resp = await githubFetch(
      env,
      `/repos/${env.GITHUB_REPO}/actions/runs?event=workflow_dispatch&per_page=10`,
    );
    if (!resp.ok) {
      await new Promise((r) => setTimeout(r, 1000));
      continue;
    }
    const data = await resp.json();
    const run = data.workflow_runs
      .filter((r) => r.name === WORKFLOW_NAME)
      .filter((r) => new Date(r.created_at).getTime() >= dispatchTime - 5000)
      .sort((a, b) => b.run_number - a.run_number)[0];
    if (run) return run;
    await new Promise((r) => setTimeout(r, 1000));
  }
  return null;
}

async function handleBuild(request, env) {
  if (!env.GITHUB_TOKEN) {
    return json({ error: "Worker is not configured with a GITHUB_TOKEN secret." }, 500);
  }

  let inputs;
  try {
    inputs = await request.json();
  } catch {
    return json({ error: "Invalid JSON body." }, 400);
  }

    // Validate + whitelist inputs. String values are interpolated into a shell
    // heredoc by the workflow, so constrain them to shell-safe charsets — this
    // also blocks command injection through a shared/public worker.
    const stringRules = {
      binding_phrase: /^[A-Za-z0-9._ -]{1,32}$/,
      device_name: /^[A-Za-z0-9_-]{1,32}$/,
      home_wifi_ssid: /^[A-Za-z0-9._ -]{1,32}$/,
      home_wifi_password: /^[A-Za-z0-9._ -]{1,63}$/,
      i18n_locales: /^[a-z, ]{0,64}$/,
    };
    for (const [k, re] of Object.entries(stringRules)) {
      const raw = inputs[k];
      if (raw == null || raw === "") continue;
      const val = String(raw).trim();
      if (!re.test(val)) {
        return json({ error: `Invalid value for ${k} (unsupported characters).` }, 400);
      }
    }
  
    const choices = {
      mixer_profile: ["0", "1", "2", "3", "4", "5", "6", "7"],
      regulatory_domain: ["EU_CE_2400", "ISM_2400"],
      zephyrus_board_rotation: ["0", "1", "2", "3", "4", "5", "6"],
    };
    for (const [k, valid] of Object.entries(choices)) {
      if (inputs[k] != null && !valid.includes(String(inputs[k]))) {
        return json({ error: `Invalid value for ${k}.` }, 400);
      }
    }
  
    // Only forward keys that exist in cloud-build.yml's dispatch inputs.
    const allowed = new Set([
      "mixer_profile",
      "regulatory_domain",
      "binding_phrase",
      "auto_wifi_on_interval",
      "zephyrus_i2c_sda",
      "zephyrus_i2c_scl",
      "zephyrus_board_rotation",
      "mushin_rx_pin",
      "mushin_tx_pin",
      "mushin_baud",
      "rcvr_uart_baud",
      "device_name",
      "home_wifi_ssid",
      "home_wifi_password",
      "i18n_locales",
    ]);

    // workflow_dispatch inputs are all strings — send every value as a string.
    // GitHub rejects non-string values with "Invalid value for input 'X'".
    const dispatchInputs = {};
    for (const [k, v] of Object.entries(inputs)) {
      if (!allowed.has(k) || v == null || v === "") continue;
      dispatchInputs[k] = String(v);
    }

  const dispatchTime = Date.now();
  const resp = await githubFetch(
    env,
    `/repos/${env.GITHUB_REPO}/actions/workflows/cloud-build.yml/dispatches`,
    {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        ref: env.GITHUB_REF || "master",
        inputs: dispatchInputs,
      }),
    },
  );

  if (resp.status !== 204) {
    const text = await resp.text();
    return json({ error: `Failed to trigger build (${resp.status}): ${text}` }, resp.status);
  }

  const run = await findNewestRun(env, dispatchTime);
  if (!run) {
    return json({ error: "Build dispatched but run not found. Check the Actions tab." }, 502);
  }

  return json({ run_id: run.id, html_url: run.html_url, status: run.status });
}

async function handleStatus(id, env) {
  const resp = await githubFetch(env, `/repos/${env.GITHUB_REPO}/actions/runs/${id}`);
  if (!resp.ok) {
    return json({ error: `Run ${id} not found (${resp.status}).` }, resp.status);
  }
  const run = await resp.json();

  const out = {
    run_id: run.id,
    status: run.status, // queued | in_progress | completed
    conclusion: run.conclusion, // success | failure | cancelled | null
    html_url: run.html_url,
    head_sha: run.head_sha,
    ready: run.status === "completed",
  };

  if (run.status === "completed" && run.conclusion === "success") {
    const a = await githubFetch(
      env,
      `/repos/${env.GITHUB_REPO}/actions/runs/${id}/artifacts`,
    );
    if (a.ok) {
      const data = await a.json();
      const art = data.artifacts.find((x) => x.name === ARTIFACT_NAME) || data.artifacts[0];
      if (art) {
        out.artifact = { name: art.name, size_download: art.size_in_bytes };
      }
    }
  }

  return json(out);
}

// GitHub 302-redirects the artifact zip to a pre-signed blob URL. Cloudflare
// Workers follows redirects and would forward the Authorization header to that
// cross-origin blob host, which Azure/S3 reject with 401. Follow manually and
// fetch the signed URL WITHOUT credentials.
async function fetchArtifact(env, archiveUrl) {
  const resp = await githubFetch(env, archiveUrl, { redirect: "manual" });
  if ([301, 302, 303, 307, 308].includes(resp.status)) {
    const loc = resp.headers.get("location");
    if (loc) {
      return fetch(loc, { headers: { "User-Agent": "pteronautos-build-worker" } });
    }
  }
  return resp;
}

async function handleDownload(id, env) {
  const a = await githubFetch(
    env,
    `/repos/${env.GITHUB_REPO}/actions/runs/${id}/artifacts`,
  );
  if (!a.ok) {
    return json({ error: `Artifacts not available (${a.status}).` }, a.status);
  }
  const data = await a.json();
  const art = data.artifacts.find((x) => x.name === ARTIFACT_NAME) || data.artifacts[0];
  if (!art || !art.archive_download_url) {
    return json({ error: "No firmware artifact found for this run." }, 404);
  }

  const zipResp = await fetchArtifact(env, art.archive_download_url);
  if (!zipResp.ok) {
    const hint =
      zipResp.status === 401
        ? " Token lacks artifact-download permission (classic PAT needs `public_repo`; fine-grained needs Actions: read)."
        : "";
    return json({ error: `Artifact download failed (${zipResp.status}).${hint}` }, zipResp.status);
  }

  const zipBytes = new Uint8Array(await zipResp.arrayBuffer());
  const files = unzipSync(zipBytes);

  const binName = Object.keys(files).find((n) => n.endsWith(".bin"));
  if (!binName) {
    return json({ error: "No .bin found inside the artifact zip." }, 502);
  }

  return new Response(files[binName], {
    status: 200,
    headers: {
      "Content-Type": "application/octet-stream",
      "Content-Disposition": 'attachment; filename="pteronautos.bin"',
      "Content-Length": String(files[binName].length),
      ...CORS,
    },
  });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: CORS });
    }

    // /api/build
    if (url.pathname === "/api/build" && request.method === "POST") {
      return handleBuild(request, env);
    }

    // /api/build/:id[/download]
    const m = url.pathname.match(/^\/api\/build\/(\d+)(\/download)?$/);
    if (m) {
      const id = Number(m[1]);
      if (m[2] === "/download") return handleDownload(id, env);
      return handleStatus(id, env);
    }

    // The worker hosts no UI. Root bounces visitors to the real flasher page
    // on GitHub Pages; unknown paths get a plain JSON 404.
    if (url.pathname === "/") {
      return Response.redirect(
        `https://${env.GITHUB_REPO.split("/")[0]}.github.io/${env.GITHUB_REPO.split("/")[1]}/flasher/`,
        302,
      );
    }

    return json({ error: "Not found." }, 404);
  },
};