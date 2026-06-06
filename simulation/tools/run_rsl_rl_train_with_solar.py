import runpy
import sys
from pathlib import Path


repo_root = Path(__file__).resolve().parents[2]
train_script = repo_root / "external" / "IsaacLab" / "scripts" / "reinforcement_learning" / "rsl_rl" / "train.py"
generated_dir = repo_root / "simulation" / ".generated"
generated_dir.mkdir(parents=True, exist_ok=True)
generated_script = generated_dir / "train_solar_rsl_rl.py"

source = train_script.read_text(encoding="utf-8")
source = source.replace(
    "# PLACEHOLDER: Extension template (do not remove this comment)",
    "import solar_lab.tasks  # noqa: F401\n# PLACEHOLDER: Extension template (do not remove this comment)",
    1,
)
generated_script.write_text(source, encoding="utf-8")

sys.path.insert(0, str(train_script.parent))
sys.argv = [str(generated_script), *sys.argv[1:]]
runpy.run_path(str(generated_script), run_name="__main__")
