import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "check_dashboard_stack.py"
spec = importlib.util.spec_from_file_location("check_dashboard_stack", SCRIPT)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DashboardStackReportTests(unittest.TestCase):
    def test_reads_exact_provisioning_frame(self):
        report = "src/dashboard_config.cpp:292:6:void dashboardHandleProvisioning()\t2128\tstatic\n"
        self.assertEqual(2128, module.stack_bytes(report, "dashboardHandleProvisioning()"))

    def test_rejects_missing_or_duplicate_report(self):
        with self.assertRaises(ValueError):
            module.stack_bytes("", "dashboardHandleProvisioning()")
        duplicate = "x:void dashboardHandleProvisioning()\t1\tstatic\nx:void dashboardHandleProvisioning()\t2\tstatic\n"
        with self.assertRaises(ValueError):
            module.stack_bytes(duplicate, "dashboardHandleProvisioning()")


if __name__ == "__main__":
    unittest.main()
