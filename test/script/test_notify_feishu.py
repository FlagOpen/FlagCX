import importlib.util
import os
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[2] / ".github/scripts/notify_feishu.py"
SPEC = importlib.util.spec_from_file_location("notify_feishu", SCRIPT)
notify_feishu = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(notify_feishu)


class NotifyFeishuTest(unittest.TestCase):
    def test_workflow_run_uses_source_event(self):
        with mock.patch.dict(
            os.environ,
            {"GITHUB_EVENT_NAME": "workflow_run", "OVERRIDE_EVENT": "pull_request"},
            clear=False,
        ):
            self.assertEqual(notify_feishu.resolve_event_name(), "pull_request")

    def test_direct_invocation_uses_github_event(self):
        with mock.patch.dict(
            os.environ,
            {"GITHUB_EVENT_NAME": "schedule", "OVERRIDE_EVENT": ""},
            clear=False,
        ):
            self.assertEqual(notify_feishu.resolve_event_name(), "schedule")


if __name__ == "__main__":
    unittest.main()
