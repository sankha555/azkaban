"""Repository-root anchored paths.

Every data path in this project (datasets, inputs, parameters, models,
configs) is written relative to the repository root.  Import `project_path`
here instead of writing a bare relative path, so the helper scripts work no
matter which directory they are run from.

    from paths import project_path
    open(project_path('data/inputs/mnist_test.txt'))
"""

from pathlib import Path

# python_helpers/paths.py -> python_helpers -> <repository root>
ROOT = Path(__file__).resolve().parents[1]


def project_path(*parts) -> str:
    """Resolve a repository-relative path against the project root.

    An absolute path is returned unchanged.
    """
    path = Path(*parts)
    return str(path if path.is_absolute() else ROOT / path)
