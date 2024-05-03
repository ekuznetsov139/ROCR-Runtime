/*
 * Copyright (C) 2016-2018 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "RDMATest.hpp"
#include "PM4Queue.hpp"
#include "PM4Packet.hpp"
#include "SDMAPacket.hpp"
#include "SDMAQueue.hpp"
#include "Dispatch.hpp"
#include "RDMAUtil.hpp"

void RDMATest::SetUp() {
    ROUTINE_START

    KFDBaseComponentTest::SetUp();

    ROUTINE_END
}

void RDMATest::TearDown() {
    ROUTINE_START

    KFDBaseComponentTest::TearDown();

    ROUTINE_END
}

TEST_F(RDMATest, GPUDirect) {
    TEST_REQUIRE_ENV_CAPABILITIES(ENVCAPS_64BITLINUX);
    TEST_START(TESTPROFILE_RUNALL);
    HSAuint64 AlternateVAGPU;

    PM4Queue queue;
    unsigned int BufferSize = PAGE_SIZE;
    int ret;

    int defaultGPUNode = m_NodeInfo.HsaDefaultGPUNode();
    ASSERT_GE(defaultGPUNode, 0) << "failed to get default GPU Node";

    HsaMemoryBuffer isaBuffer(PAGE_SIZE, defaultGPUNode);
    HsaMemoryBuffer srcSysBuffer(BufferSize, defaultGPUNode, false);
    HsaMemoryBuffer srcLocalBuffer(BufferSize, defaultGPUNode, false, true);

    ASSERT_SUCCESS(hsaKmtMapMemoryToGPU(srcSysBuffer.As<void*>(),
                                        srcSysBuffer.Size(),
                                        &AlternateVAGPU));
    ASSERT_SUCCESS(hsaKmtMapMemoryToGPU(srcLocalBuffer.As<void*>(),
                                        srcLocalBuffer.Size(),
                                        &AlternateVAGPU));

    /* Fill up srcSysBuffer */
    srcSysBuffer.Fill(0xfe);

    /* Put 'copy dword' command to ISA buffer */
    ASSERT_SUCCESS(m_pAsm->RunAssembleBuf(CopyDwordIsa, isaBuffer.As<char*>()));


    ASSERT_SUCCESS(queue.Create(defaultGPUNode));
    Dispatch dispatch(isaBuffer);

    /* Submit the command to GPU so GPU will copy from system memory
     * (srcSysBuffer) to local memory(srcLocalBuffer)
     */
    dispatch.SetArgs(srcSysBuffer.As<void*>(), srcLocalBuffer.As<void*>());
    dispatch.Submit(queue);
    dispatch.Sync(g_TestTimeOut);  // GPU executed the command

    EXPECT_SUCCESS(queue.Destroy());

    LocalMemoryAccess Rdma;

    Rdma.Open();
    ASSERT_GE(Rdma.fd, 0) << "Failed to open RDMA";

    /* GetPages asks the test driver to convert GPU virtual memory to DMA/
     * Physical memory and save it in the list. rdma_mmap maps the memory to
     * user space memory.
     */
    ret = Rdma.GetPages((uint64_t)srcLocalBuffer.As<void*>(), PAGE_SIZE);
    ASSERT_EQ(ret, 0) << "Failed to get pages";

    void *gpuAddr = Rdma.MMap((uint64_t)srcLocalBuffer.As<void*>(), PAGE_SIZE);
    ASSERT_GE((uint64_t)gpuAddr, 0) << "Failed to map RDMA address.";

    /* Read the memory to confirm that application can read the local memory
     * correctly from the mapped address.
     */
    EXPECT_EQ(memcmp(gpuAddr, srcSysBuffer.As<void*>(), 4), 0);

    Rdma.UnMap(gpuAddr, PAGE_SIZE);
    Rdma.Close();

    TEST_END
}
