"""CPython build for the native `uUR` module.

Compiles the cUR core, the bundled SHA-256 and the binding in `python/uUR.c`
into one `uUR` extension: the CPython counterpart of `micropython.mk`.
"""

from __future__ import annotations

import glob
import os
import tempfile

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

WARN_UNUSED_RESULT_FLAG = "-Werror=unused-result"


def _accepts_flag(compiler, flag: str) -> bool:
    """Whether `compiler` compiles a trivial file with `flag`.

    Probed rather than inferred from compiler_type: that reports "unix" for
    Intel oneAPI, NVIDIA HPC and Cray as well, where the flag is unrecognised
    and would fail the whole build instead of just losing the check. A failed
    probe prints the compiler's own error - that is the probe working, not a
    build problem. Mirrors the CMAKE_C_COMPILER_ID gate in CMakeLists.txt.
    """
    with tempfile.TemporaryDirectory() as tmpdir:
        source = os.path.join(tmpdir, "flag_probe.c")
        with open(source, "w", encoding="utf-8") as handle:
            handle.write("int main(void) { return 0; }\n")
        try:
            compiler.compile([source], output_dir=tmpdir, extra_postargs=[flag])
        except Exception:
            return False
    return True


class WarnUnusedResultBuildExt(build_ext):
    """Make ignored annotated results fatal where the compiler supports it."""

    def build_extensions(self) -> None:
        if _accepts_flag(self.compiler, WARN_UNUSED_RESULT_FLAG):
            for extension in self.extensions:
                extension.extra_compile_args = [
                    *(extension.extra_compile_args or []),
                    WARN_UNUSED_RESULT_FLAG,
                ]
        super().build_extensions()

# Source paths MUST be relative to this setup.py directory — setuptools' PEP 517
# wheel build rejects absolute paths. glob runs with cwd == the project root
# (the source tree pip builds in), so relative globs resolve correctly.
core_sources = sorted(
    glob.glob("src/*.c")
    + glob.glob("src/types/*.c")
    + glob.glob("src/sha256/sha256.c")
)

uur_ext = Extension(
    name="uUR",
    sources=["python/uUR.c", *core_sources],
    # cUR root ('.') satisfies the binding's `src/...` includes; 'src' satisfies
    # the core's sibling includes (e.g. "sha256/sha256.h", "ur_decoder.h").
    include_dirs=[".", "src"],
)

setup(
    ext_modules=[uur_ext],
    cmdclass={"build_ext": WarnUnusedResultBuildExt},
)
