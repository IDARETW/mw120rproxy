"""Offline checks for local shader extraction validation; no process is opened."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from setup_shader_templates import validate_program

class ShaderValidationTests(unittest.TestCase):
    def fixture(self,program):
        header=bytearray(40);struct.pack_into('<I',header,32,len(program))
        return {'name':'test','header':header.hex(),'program_sha256':hashlib.sha256(program).hexdigest()}

    def test_accepts_matching_program_and_empty_null_shader(self):
        for data in (b'DXBC'+bytes(32),b''):
            validate_program(self.fixture(data),data)

    def test_rejects_truncated_and_changed_program(self):
        data=b'DXBC'+bytes(32);shader=self.fixture(data)
        for bad in (data[:-1],data[:-1]+b'X'):
            with self.assertRaises(ValueError):validate_program(shader,bad)

    def test_rejects_non_shader_even_with_matching_hash(self):
        with self.assertRaises(ValueError):validate_program(self.fixture(b'nope'),b'nope')

if __name__=='__main__':unittest.main()
