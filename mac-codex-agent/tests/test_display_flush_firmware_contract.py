import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class DisplayFlushFirmwareContractTests(unittest.TestCase):
    def test_lvgl_flush_batches_framebuffer_until_last_area(self):
        source = (ROOT / "main/hal/hal_display.cpp").read_text()

        self.assertIn("enableFrameBuffer(false);", source)
        self.assertNotIn("enableFrameBuffer(true);", source)

        flush_body = source.split("static void lvgl_flush_cb", 1)[1]
        flush_body = flush_body.split("static void lvgl_read_cb", 1)[0]
        last_flush = "if (lv_display_flush_is_last(disp))"
        self.assertIn(last_flush, flush_body)

        commit_body = flush_body.split(last_flush, 1)[1]
        commit_body = commit_body.split("}", 1)[0]
        self.assertIn("gfx.display();", commit_body)
        self.assertLess(
            flush_body.index("gfx.display();"),
            flush_body.index("lv_display_flush_ready(disp)"),
        )


if __name__ == "__main__":
    unittest.main()
