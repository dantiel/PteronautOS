import os
import subprocess

def git_cmd(*args):
    return subprocess.check_output(["git"] + list(args), stderr=subprocess.DEVNULL).decode("utf-8").rstrip('\r\n')


def get_git_version():
    """
    Return a dict with keys
    version: the semver tag if HEAD is exactly tagged ("0.2.0"), a monotonic
        describe otherwise ("0.2.0-3-g7edeab6"), or the branch name as final
        fallback when no tags exist
    sha: the 6 character short sha for the current HEAD revision, falling back to
        VERSION file if not in a git repo
    """
    ver = "ver.unknown"
    sha = "000000"

    try:
        sha = git_cmd("rev-parse", "HEAD")
        # Prefer a semver description (tag or tag-N-gSHA); --always keeps a
        # usable value even in a shallow/untagged checkout.
        try:
            ver = git_cmd("describe", "--tags", "--always")
        except:
            ver = git_cmd("rev-parse", "--abbrev-ref", "HEAD")
    except:
        if os.path.exists("VERSION"):
            with open("VERSION") as _f:
                data = _f.readline()
                _f.close()
            sha = data.split()[1].strip()

    return dict(version=ver, sha=sha[:6])


class ElrsUploadResult:
        # SUCCESS
        Success = 0
        # ERROR: Unspecified
        ErrorGeneral = -1
        # ERROR: target mismatch
        ErrorMismatch = -2