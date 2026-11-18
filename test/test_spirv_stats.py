# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/spirv_stats.py: the figures it gives for a translated shader's SPIR-V."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
import spirv_stats  # noqa: E402

# A small module in the shape the translator emits: a register in a Function variable, a guest
# memory load through a converted pointer, a push-constant load, a branch and a select.
MODULE = '''\
               OpCapability Shader
               OpCapability Int64
               OpCapability PhysicalStorageBufferAddresses
               OpExtension "SPV_KHR_physical_storage_buffer"
               OpMemoryModel PhysicalStorageBuffer64 GLSL450
               OpEntryPoint GLCompute %1 "main" %8
               OpExecutionMode %1 LocalSize 64 1 1
               OpDecorate %6 Block
               OpMemberDecorate %6 0 Offset 0
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %4 = OpTypeInt 32 0
          %5 = OpTypeInt 64 0
          %6 = OpTypeStruct %4
          %7 = OpTypePointer PushConstant %6
          %8 = OpVariable %7 PushConstant
          %9 = OpTypePointer PushConstant %4
         %10 = OpTypePointer Function %4
         %11 = OpTypePointer PhysicalStorageBuffer %4
         %12 = OpTypeBool
         %13 = OpConstant %4 0
         %14 = OpConstant %4 1
          %1 = OpFunction %2 None %3
         %15 = OpLabel
         %16 = OpVariable %10 Function %13
         %17 = OpAccessChain %9 %8 %13
         %18 = OpLoad %4 %17
               OpStore %16 %18
         %19 = OpLoad %4 %16
         %20 = OpUConvert %5 %19
         %21 = OpConvertUToPtr %11 %20
         %22 = OpLoad %4 %21 Aligned 4
         %23 = OpIEqual %12 %22 %13
         %24 = OpSelect %4 %23 %14 %13
               OpSelectionMerge %26 None
               OpBranchConditional %23 %25 %26
         %25 = OpLabel
               OpStore %21 %24 Aligned 4
               OpBranch %26
         %26 = OpLabel
               OpReturn
               OpFunctionEnd
'''


class Figures(unittest.TestCase):
    def test_disassembly_figures(self):
        f = spirv_stats.disassembly_figures(MODULE)
        self.assertEqual(f['reg_ld'], 1)
        self.assertEqual(f['reg_st'], 1)
        self.assertEqual(f['mem_ld'], 1)
        self.assertEqual(f['mem_st'], 1)
        self.assertEqual(f['push_ld'], 1)
        self.assertEqual(f['branches'], 1)
        self.assertEqual(f['selects'], 1)
        self.assertEqual(f['image'], 0)
        # the function's instructions, labels left out: 15 from OpVariable to OpReturn
        self.assertEqual(f['insts'], 15)

    def test_shader_key(self):
        self.assertEqual(spirv_stats.shader_key('pixel-20d4653a00.spv'), ('p', 0x20d4653a00))
        self.assertEqual(spirv_stats.shader_key('compute-20d1b00800-0123456789abcdef.spv'), ('c', 0x20d1b00800))
        self.assertIsNone(spirv_stats.shader_key('hull-20.spv'))
        self.assertIsNone(spirv_stats.shader_key('pixel-xyz.spv'))

    def test_read_counts(self):
        with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
            f.write('p 20d4653a00 7 100 0\nv 20d0000100 3 9 0\np 20d4653a00 2 1 0\n')
        self.addCleanup(os.unlink, f.name)
        self.assertEqual(spirv_stats.read_counts(f.name), {('p', 0x20d4653a00): 9, ('v', 0x20d0000100): 3})


@unittest.skipUnless(shutil.which('spirv-as') and shutil.which('spirv-dis') and shutil.which('spirv-val') and
                     shutil.which('spirv-opt'), 'SPIRV-Tools not installed')
class Survey(unittest.TestCase):
    def test_survey_ranks_by_calls(self):
        with tempfile.TemporaryDirectory() as d:
            source = os.path.join(d, 'm.spvasm')
            with open(source, 'w') as f:
                f.write(MODULE)
            for name in ('compute-100.spv', 'compute-200.spv'):
                subprocess.run(['spirv-as', '--target-env', 'vulkan1.2', source, '-o', os.path.join(d, name)], check=True)
            records = spirv_stats.survey(d, optimise=True, counts={('c', 0x200): 5})
            self.assertEqual([r['name'] for r in records], ['compute-100.spv', 'compute-200.spv'])
            self.assertTrue(all(r['figures']['valid'] for r in records))
            self.assertEqual([r['calls'] for r in records], [0, 5])
            # the optimiser promotes the register variable
            self.assertEqual(records[0]['optimised']['reg_ld'], 0)
            self.assertLess(records[0]['optimised']['insts'], records[0]['figures']['insts'])


if __name__ == '__main__':
    unittest.main()
