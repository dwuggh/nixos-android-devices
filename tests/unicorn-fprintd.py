"""Real patched daemon/D-Bus with libfprint's storage boundary fixture.

Only the virtual storage driver's identity is set to unicorn in the test build;
production daemon policy and storage/deletion callbacks are unchanged.
"""
import unittest
import fprintd as upstream
from gi.repository import FPrint

class Protection(upstream.FPrintdVirtualStorageDeviceBaseTest):
    device_driver = 'unicorn'

    def setUp(self):
        super().setUp()
        self.device.Claim('(s)', 'testuser')

    def tearDown(self):
        self.try_release()
        super().tearDown()

    def test_full_preserves_orphans(self):
        self.send_command('INSERT', 'orphan')
        self.send_image('new-finger')
        self.send_command('ERROR', int(FPrint.DeviceError.DATA_FULL))
        self.device.EnrollStart('(s)', 'right-thumb')
        self.wait_for_result('enroll-data-full')
        self.device.EnrollStop()
        self.assertEqual(self.get_stored_prints(), ['orphan'])

    def test_orphan_duplicate_is_preserved(self):
        self.send_command('INSERT', 'orphan')
        self.device.EnrollStart('(s)', 'right-thumb')
        self.send_image('orphan')
        self.wait_for_result('enroll-duplicate')
        self.device.EnrollStop()
        self.assertEqual(self.get_stored_prints(), ['orphan'])

    def test_explicit_delete_one_finger(self):
        self.send_command('INSERT', 'orphan')
        self.enroll_print('finger-a', 'left-index-finger')
        self.enroll_print('finger-b', 'right-index-finger')
        self.device.DeleteEnrolledFinger('(s)', 'left-index-finger')
        self.assertEqual(set(self.get_stored_prints()), {'orphan', 'finger-b'})
        self.assertEqual(self.device.ListEnrolledFingers('(s)', 'testuser'), ['right-index-finger'])

    def test_existing_finger_requires_explicit_delete(self):
        self.enroll_print('finger-a', 'left-index-finger')
        with self.assertFprintError('AlreadyInUse'):
            self.device.EnrollStart('(s)', 'left-index-finger')
        self.assertEqual(self.get_stored_prints(), ['finger-a'])

class FirstEnrollment(upstream.FPrintdVirtualStorageDeviceBaseTest):
    socket_env = 'FP_VIRTUAL_DEVICE_STORAGE_NO_LIST'
    device_driver = 'unicorn'

    def test_first_enrollment_preserves_storage(self):
        self.device.Claim('(s)', 'testuser')
        try:
            self.send_command('INSERT', 'orphan')
            self.enroll_print('new-finger', 'left-index-finger')
            self.assertEqual(set(self.get_stored_prints()), {'orphan', 'new-finger'})
        finally:
            self.try_release()

if __name__ == '__main__':
    unittest.main()
