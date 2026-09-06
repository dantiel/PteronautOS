// PteronautOS hardware table — sortable columns + live filter.
(function () {
  "use strict";

  function init() {
    var table = document.querySelector("[data-hw-table]");
    if (!table) return;

    var tbody = table.querySelector("tbody");
    var filter = document.querySelector("[data-hw-filter]");
    var count = document.querySelector("[data-hw-count]");
    var countLabel = count ? (count.getAttribute("data-count-label") || "receivers shown") : "receivers shown";

    var headers = Array.prototype.slice.call(table.querySelectorAll("th[data-sort]"));

    function rows() {
      return Array.prototype.slice.call(tbody.querySelectorAll("tr"));
    }

    function visibleRows() {
      return rows().filter(function (tr) {
        return tr.style.display !== "none";
      });
    }

    function updateCount() {
      if (!count) return;
      var n = visibleRows().length;
      count.textContent = n + " · " + countLabel;
    }

    // ── Sorting ──────────────────────────────────────────────────────
    headers.forEach(function (th) {
      th.classList.add("sortable");
      th.setAttribute("role", "button");
      th.setAttribute("tabindex", "0");
      th.addEventListener("click", function () { sortBy(th); });
      th.addEventListener("keydown", function (e) {
        if (e.key === "Enter" || e.key === " ") { e.preventDefault(); sortBy(th); }
      });
    });

    var sortState = { key: null, dir: 1 };

    function sortBy(th) {
      var key = th.getAttribute("data-sort");
      var type = th.getAttribute("data-type") || "alpha";
      var colIndex = headers.indexOf(th);

      sortState.dir = sortState.key === key ? -sortState.dir : 1;
      sortState.key = key;

      var rs = rows();
      rs.sort(function (a, b) {
        var av = a.children[colIndex].getAttribute("data-val") || a.children[colIndex].textContent.trim();
        var bv = b.children[colIndex].getAttribute("data-val") || b.children[colIndex].textContent.trim();
        if (type === "num") {
          return (parseFloat(av) - parseFloat(bv)) * sortState.dir;
        }
        return av.localeCompare(bv, undefined, { sensitivity: "base", numeric: true }) * sortState.dir;
      });

      rs.forEach(function (tr) { tbody.appendChild(tr); });

      headers.forEach(function (h) { h.removeAttribute("aria-sort"); });
      th.setAttribute("aria-sort", sortState.dir === 1 ? "ascending" : "descending");
    }

    // ── Filtering ────────────────────────────────────────────────────
    if (filter) {
      filter.addEventListener("input", function () {
        var q = filter.value.trim().toLowerCase();
        rows().forEach(function (tr) {
          var text = tr.textContent.toLowerCase();
          tr.style.display = !q || text.indexOf(q) !== -1 ? "" : "none";
        });
        updateCount();
      });
    }

    updateCount();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
