//===============================================================================
// Copyright (c) 2014-2018  Advanced Micro Devices, Inc. All rights reserved.
//===============================================================================
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
//
//  File Name:   Codec_GT.cpp
//  Description: implementation of the CCodec_GT class
//
//////////////////////////////////////////////////////////////////////////////

#pragma warning(disable:4100)    // Ignore warnings of unreferenced formal parameters

#ifdef _WIN32
#include "Common.h"
#include "Codec_GT.h"
#include <process.h>

#ifdef GT_COMPDEBUGGER
#include "CompClient.h"
#endif

//======================================================================================
// #define USE_PRINTF
// #define USE_NOMULTITHREADING 

#ifdef USE_FILEIO
    #include <stdio.h>
    FILE * gt_File = NULL;
    int gt_blockcount = 0;
    int gt_total_MSE = 0;
#endif

//
// Thread procedure for encoding a block
//
// The thread stays alive, and expects blocks to be pushed to it by a producer
// process that signals it when new work is available. When the producer is finished
// it should set the exit flag in the parameters to allow the tread to quit
//

unsigned int    _stdcall GTCThreadProcEncode(void* param)
{
    GTCEncodeThreadParam *tp = (GTCEncodeThreadParam*)param;

    while(tp->exit == FALSE)
    {
        if(tp->run == TRUE)
        {
            tp->encoder->CompressBlock(tp->in, tp->out);
            tp->run = false;
        }
        using namespace chrono;

        std::this_thread::sleep_for(0ms);
    }

    return 0;
}


static GTCEncodeThreadParam *g_EncodeParameterStorage = NULL;


//////////////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////////////

CCodec_GTC::CCodec_GTC() : CCodec_DXTC(CT_GTC)
{
    m_LibraryInitialized   = false;

    m_Use_MultiThreading = true;
    m_NumThreads = 8;
    m_NumEncodingThreads   = m_NumThreads;
    m_EncodingThreadHandle = NULL;
    m_LiveThreads          = 0;
    m_LastThread           = 0;

    m_quality = 0.05f;
}


bool CCodec_GTC::SetParameter(const CMP_CHAR* pszParamName, CMP_CHAR* sValue)
{
    if (sValue == NULL) return false;

    if(strcmp(pszParamName, "NumThreads") == 0)
    {
        m_NumThreads = (CMP_BYTE) std::stoi(sValue) & 0xFF;
        m_Use_MultiThreading = m_NumThreads > 1;
    }
    else
        if (strcmp(pszParamName, "Quality") == 0)
        {
            m_quality = std::stof(sValue);
            if ((m_quality < 0) || (m_quality > 1.0))
            {
                return false;
            }
        }
        else
        return CCodec_DXTC::SetParameter(pszParamName, sValue);
    return true;
}



bool CCodec_GTC::SetParameter(const CMP_CHAR* pszParamName, CMP_DWORD dwValue)
{
    if(strcmp(pszParamName, "NumThreads") == 0)
    {
        m_NumThreads = (CMP_BYTE) dwValue;
        m_Use_MultiThreading = m_NumThreads > 1;
    }
    else
        return CCodec_DXTC::SetParameter(pszParamName, dwValue);
    return true;
}

bool CCodec_GTC::SetParameter(const CMP_CHAR* pszParamName, CODECFLOAT fValue)
{
    if (strcmp(pszParamName, "Quality") == 0)
        m_quality = fValue;
    else
        return CCodec_DXTC::SetParameter(pszParamName, fValue);

    return true;
}


CCodec_GTC::~CCodec_GTC()
{
    if (m_LibraryInitialized)
    {

        if (m_Use_MultiThreading)
        {
            // Tell all the live threads that they can exit when they have finished any current work
            for(int i=0; i < m_LiveThreads; i++)
            {
                // If a thread is in the running state then we need to wait for it to finish
                // any queued work from the producer before we can tell it to exit.
                //
                // If we don't wait then there is a race condition here where we have
                // told the thread to run but it hasn't yet been scheduled - if we set
                // the exit flag before it runs then its block will not be processed.
#pragma warning(push)
#pragma warning(disable:4127) //warning C4127: conditional expression is constant
                while (1)
                {
                    if (m_EncodeParameterStorage[i].run != TRUE)
                    {
                        break;
                    }
                }
#pragma warning(pop)
                // Signal to the thread that it can exit
                m_EncodeParameterStorage[i].exit = TRUE;
            }

            // Now wait for all threads to have exited
            if (m_LiveThreads > 0)
            {
                for (CMP_DWORD dwThread = 0; dwThread < m_LiveThreads; dwThread++)
                {
                    std::thread& curThread = m_EncodingThreadHandle[dwThread];

                    curThread.join();
                }
            }

            for (unsigned int i = 0; i < m_LiveThreads; i++)
            {
                std::thread& curThread = m_EncodingThreadHandle[i];

                curThread = std::thread();
            }


        } // MultiThreading

        m_EncodingThreadHandle = NULL;

        if (m_EncodeParameterStorage)
            delete[] m_EncodeParameterStorage;
        m_EncodeParameterStorage = NULL;


        for (int i = 0; i < m_NumEncodingThreads; i++)
        {
            if (m_encoder[i])
            {
                delete m_encoder[i];
                m_encoder[i] = NULL;
            }
        }

        if (m_decoder)
        {
            delete m_decoder;
            m_decoder = NULL;
        }

        m_LibraryInitialized = false;
    }
}



CodecError CCodec_GTC::InitializeGTCLibrary()
{
    if (!m_LibraryInitialized)
    {

        for(CMP_DWORD i=0; i < MAX_GT_THREADS; i++)
        {
            m_encoder[i] = NULL;
        }

        // Create threaded encoder instances
        m_LiveThreads = 0;
        m_LastThread  = 0;
        m_NumEncodingThreads = min(m_NumThreads, MAX_GT_THREADS);
        if (m_NumEncodingThreads == 0) m_NumEncodingThreads = 1; 
        m_Use_MultiThreading = m_NumEncodingThreads > 1;

        m_EncodeParameterStorage = new GTCEncodeThreadParam[m_NumEncodingThreads];
        if (!m_EncodeParameterStorage)
        {
            return CE_Unknown;
        }

        m_EncodingThreadHandle = new std::thread[m_NumEncodingThreads];
        if (!m_EncodingThreadHandle)
        {
            delete[] m_EncodeParameterStorage;
            m_EncodeParameterStorage = NULL;

            return CE_Unknown;
        }

        CMP_DWORD   i;

        for(i=0; i < m_NumEncodingThreads; i++)
        {
            // Create single encoder instance
            m_encoder[i] = new GTCBlockEncoder(m_quality);

            
            // Cleanup if problem!
            if (!m_encoder[i])
            {

                delete[] m_EncodeParameterStorage;
                m_EncodeParameterStorage = NULL;

                delete[] m_EncodingThreadHandle;
                m_EncodingThreadHandle = NULL;

                for (CMP_DWORD j = 0; j<i; j++)
                {
                    delete m_encoder[j];
                    m_encoder[j] = NULL;
                }

                return CE_Unknown;
            }

            #ifdef USE_DBGTRACE
            DbgTrace(("Encoder[%d]:ModeMask %X, Quality %f\n",i,m_ModeMask,m_Quality));
            #endif

        }

        // Create the encoding threads in the suspended state
        for (i = 0; i<m_NumEncodingThreads; i++)
        {
            // Initialize thread parameters.
            m_EncodeParameterStorage[i].encoder = m_encoder[i];
            // Inform the thread that at the moment it doesn't have any work to do
            // but that it should wait for some and not exit
            m_EncodeParameterStorage[i].run = FALSE;
            m_EncodeParameterStorage[i].exit = FALSE;

            m_EncodingThreadHandle[i] = std::thread(
                GTCThreadProcEncode,
                (void*)&m_EncodeParameterStorage[i]
            );
            m_LiveThreads++;
        }


        // Create single decoder instance
        m_decoder = new GTCBlockDecoder();
        if(!m_decoder)
        {
            for (CMP_DWORD j = 0; j<m_NumEncodingThreads; j++)
            {
                delete m_encoder[j];
                m_encoder[j] = NULL;
            }
            return CE_Unknown;
        }

        m_LibraryInitialized = true;
    }
    return CE_OK;
}


CodecError CCodec_GTC::EncodeGTCBlock(CMP_BYTE *in,  CMP_BYTE *out)
{
#ifdef USE_NOMULTITHREADING
    m_Use_MultiThreading = false;
#endif

if (m_Use_MultiThreading)
{
    CMP_WORD   threadIndex;

    // Loop and look for an available thread
    CMP_BOOL found = FALSE;
    threadIndex = m_LastThread;
    while (found == FALSE)
    {

        if (m_EncodeParameterStorage == NULL)
            return CE_Unknown;

        if(m_EncodeParameterStorage[threadIndex].run == FALSE)
        {
            found = TRUE;
            break;
        }

        // Increment and wrap the thread index
        threadIndex++;
        if(threadIndex == m_LiveThreads)
        {
            threadIndex = 0;
        }
    }

    m_LastThread = threadIndex;

    // Copy the input data into the thread storage
    memcpy(m_EncodeParameterStorage[threadIndex].in,in,m_xdim * m_ydim * 4 * sizeof(CMP_BYTE));

    // Set the output pointer for the thread to the provided location
    m_EncodeParameterStorage[threadIndex].out = out;

    // Tell the thread to start working
    m_EncodeParameterStorage[threadIndex].run = TRUE;
}
else 
{
        // Copy the input data into the thread storage
        memcpy(m_EncodeParameterStorage[0].in, in, m_xdim * m_ydim * 4  * sizeof(CMP_BYTE));

        // Set the output pointer for the thread to write
        m_EncodeParameterStorage[0].out = out;
        m_encoder[0]->CompressBlock(m_EncodeParameterStorage[0].in,m_EncodeParameterStorage[0].out);
}
    return CE_OK;
}

CodecError CCodec_GTC::FinishGTCEncoding(void)
{
    if(!m_LibraryInitialized)
    {
        return CE_Unknown;
    }

    if (!m_EncodeParameterStorage)
    {
        return CE_Unknown;
    }

if (m_Use_MultiThreading)
{
    // Wait for all the live threads to finish any current work
    for(CMP_DWORD i=0; i < m_LiveThreads; i++)
    {

        // If a thread is in the running state then we need to wait for it to finish
        // its work from the producer
        while (m_EncodeParameterStorage[i].run == TRUE)
        {
            std::this_thread::sleep_for(1ms);
        }
    }
}
return CE_OK;
}

CodecError CCodec_GTC::Compress(CCodecBuffer& bufferIn, CCodecBuffer& bufferOut, Codec_Feedback_Proc pFeedbackProc, CMP_DWORD_PTR pUser1, CMP_DWORD_PTR pUser2)
{
    CodecError err = InitializeGTCLibrary();
    if (err != CE_OK) return err;

    // Source image size
    int xsize = bufferIn.GetWidth();
    int ysize = bufferIn.GetHeight();
    int zsize = 1; //todo: add depth to support 3d textures

    // Block sizes to partition the source data into for compression
    m_xdim = bufferOut.GetBlockWidth();
    m_ydim = bufferOut.GetBlockHeight();
    m_zdim = 1;

    CodecError result = CE_OK;
    int xdim = m_xdim;
    int ydim = m_ydim;
    int zdim = m_zdim;

    g_GTCEncode.m_xdim = m_xdim;
    g_GTCEncode.m_ydim = m_ydim;
    g_GTCEncode.m_zdim = m_zdim;

    uint8_t *bufferOutput = bufferOut.GetData();

    int x, y, z, i;
    int xblocks = (xsize + m_xdim - 1) / m_xdim;
    int yblocks = (ysize + m_ydim - 1) / m_ydim;
    int zblocks = (zsize + m_zdim - 1) / m_zdim;
    int offset;
    int processingBlock = 0;

    float TotalBlocks = (float)(yblocks * xblocks);

    CMP_BYTE *blockToEncode;
    CMP_BYTE *srcBlock = (CMP_BYTE *)malloc(xdim*ydim * 4);

    for (z = 0; z < zblocks; z++)
    {
        for (y = 0; y < yblocks; y++)
        {
            for (x = 0; x < xblocks; x++)
            {
                processingBlock++;

                // Output block size for GTC is fixed at 16 bytes
                offset = ((z * yblocks + y) * xblocks + x) * 16;
                uint8_t *bp = bufferOutput + offset;
                memset(srcBlock, 0, sizeof(srcBlock));
                bufferIn.ReadBlockRGBA(x * m_xdim, y * m_ydim, m_xdim, m_ydim, srcBlock);

                EncodeGTCBlock(srcBlock, bp);
            }

            if (pFeedbackProc)
            {
                if ((processingBlock % 10) == 0)
                {
                    float fProgress = 100.f * ((float)(processingBlock) / TotalBlocks);
                    if (pFeedbackProc(fProgress, pUser1, pUser2))
                    {
                        result = CE_Aborted;
                        break;
                    }
                }
            }

        }
    }

    free(srcBlock);

    CodecError EncodeResult = FinishGTCEncoding();

    if (result != CE_Aborted)
        result = EncodeResult;

    return result;
}

#ifdef USE_FILEIO_DECODE
FILE * gt_File_Decode = NULL;
char ModesUsed[CMP_MAXGTMODES+1];
#endif

CodecError CCodec_GTC::Decompress(CCodecBuffer& bufferIn, CCodecBuffer& bufferOut, Codec_Feedback_Proc pFeedbackProc, CMP_DWORD_PTR pUser1, CMP_DWORD_PTR pUser2)
{
    CodecError err = InitializeGTCLibrary();
    if (err != CE_OK) return err;

    m_xdim = bufferIn.GetBlockWidth();
    m_ydim = bufferIn.GetBlockHeight();
    m_zdim = 1;

    if (m_xdim == 0) m_xdim = 4;
    if (m_ydim == 0) m_ydim = 4;

    // Our Compressed data Blocks are always 128 bit long (4x4 blocks)
    const CMP_DWORD imageWidth = bufferIn.GetWidth();
    const CMP_DWORD imageHeight = bufferIn.GetHeight();
    const CMP_DWORD imageDepth = 1;
    const CMP_BYTE  bitness = 8;

    const CMP_DWORD CompBlockX   = m_xdim;
    const CMP_DWORD CompBlockY   = m_ydim;
    CMP_BYTE  Block_Width        = m_xdim;
    CMP_BYTE  Block_Height       = m_ydim;

    const CMP_DWORD dwBlocksX   = ((bufferIn.GetWidth() + (CompBlockX - 1)) / CompBlockX);
    const CMP_DWORD dwBlocksY   = ((bufferIn.GetHeight() + (CompBlockY - 1)) / CompBlockY);
    const CMP_DWORD dwBlocksZ   = 1;
    const CMP_DWORD dwBufferInDepth = 1;

    // Override the current input buffer Pitch size  (Since it will be set according to the Compressed Block Sizes
    // and not to the Compressed Codec data which is for GTC 16 Bytes per block x Number of blocks per row
    bufferIn.SetPitch(16 * dwBlocksX);

    // Output data size Pitch
    CMP_DWORD  dwPitch = bufferOut.GetPitch();

    // Output Buffer
    CMP_BYTE *pDataOut = bufferOut.GetData();


    const CMP_DWORD dwBlocksXY = dwBlocksX*dwBlocksY;

    for (CMP_DWORD cmpRowY = 0; cmpRowY < dwBlocksY; cmpRowY++)        // Compressed images row = height
    {
        for (CMP_DWORD cmpColX = 0; cmpColX < dwBlocksX; cmpColX++)    // Compressed images Col = width
        {
            union FBLOCKS
            {
                CMP_BYTE decodedBlock[144][4];            // max 12x12 block size
                CMP_BYTE destBlock[576];                  // max 12x12x4
            } DecData;

            union BBLOCKS
            {
                CMP_DWORD           compressedBlock[4];
                CMP_BYTE            out[16];
                CMP_BYTE            in[16];
            } CompData;

            bufferIn.ReadBlock(cmpColX * 4, cmpRowY * 4, CompData.compressedBlock, 4);

            // Encode to the appropriate location in the compressed image
            m_decoder->DecompressBlock(DecData.decodedBlock, CompData.in);

            // Now that we have a decoded block lets copy that data over to the target image buffer
            CMP_DWORD outCol = cmpColX*Block_Width;
            CMP_DWORD outRow = cmpRowY*Block_Height;
            CMP_DWORD outImgRow = outRow;
            CMP_DWORD outImgCol = outCol;

            for (int row = 0; row < Block_Height; row++)
            {
                CMP_DWORD  nextRowCol = (outRow + row)*dwPitch + (outCol * 4);
                CMP_BYTE*  pData = (CMP_BYTE*)(pDataOut + nextRowCol);
                if ((outImgRow + row) < imageHeight)
                {
                    outImgCol = outCol;
                    for (int col = 0; col < Block_Width; col++)
                    {
                        CMP_DWORD w = outImgCol + col;
                        if (w < imageWidth)
                        {
                            int index = row*Block_Width + col;
                            *pData++ = (CMP_BYTE)DecData.decodedBlock[index][BC_COMP_RED];
                            *pData++ = (CMP_BYTE)DecData.decodedBlock[index][BC_COMP_GREEN];
                            *pData++ = (CMP_BYTE)DecData.decodedBlock[index][BC_COMP_BLUE];
                            *pData++ = (CMP_BYTE)DecData.decodedBlock[index][BC_COMP_ALPHA];
                        }
                        else break;
                    }
                }
            }
        }

        if (pFeedbackProc)
        {
            float fProgress = 100.f * (cmpRowY * dwBlocksX) / dwBlocksXY;
            if (pFeedbackProc(fProgress, pUser1, pUser2))
            {
                return CE_Aborted;
            }
        }
    }


/**

    const CMP_DWORD dwBlocksX = ((bufferIn.GetWidth() + 3) >> 2);
    const CMP_DWORD dwBlocksY = ((bufferIn.GetHeight() + 3) >> 2);
    const CMP_DWORD dwBlocksXY = dwBlocksX*dwBlocksY;

#ifdef USE_FILEIO_DECODE
    gt_File_Decode = fopen("gt_report_decode.txt","w");
    memset(ModesUsed,'.',CMP_MAXGTMODES);
#endif

    for(CMP_DWORD j = 0; j < dwBlocksY; j++)
    {
        for(CMP_DWORD i = 0; i < dwBlocksX; i++)
        {

            union FBLOCKS
            {
                CMP_BYTE decodedBlock[16][4];
                CMP_BYTE destBlock[BLOCK_SIZE_4X4X4];
            } DecData;

            union BBLOCKS
            {
                CMP_DWORD       compressedBlock[4];
                CMP_BYTE            out[16];
                CMP_BYTE            in[16];
            } CompData;

            CMP_BYTE destBlock[BLOCK_SIZE_4X4X4];


#ifdef USE_FILEIO_DECODE
            // Mode Used
            int mode = CompData.in[CMP_GT_MODE];
            if (mode > CMP_MAXGTMODES)
            {
                printf("Err mode = %d\n",mode);
            }
            else
            ModesUsed[mode] = 'x';
#endif

            bufferIn.ReadBlock(i*4, j*4, CompData.compressedBlock, 4);

            // Encode to the appropriate location in the compressed image
            m_decoder->DecompressBlock(DecData.decodedBlock,CompData.in);

            // Create the block for decoding
            int srcIndex = 0;
            for(int row=0; row < BLOCK_SIZE_4; row++)
            {
                for(int col=0; col<BLOCK_SIZE_4; col++)
                {
                    destBlock[srcIndex]   = (CMP_BYTE)DecData.decodedBlock[row*BLOCK_SIZE_4+col][BC_COMP_RED];
                    destBlock[srcIndex+1] = (CMP_BYTE)DecData.decodedBlock[row*BLOCK_SIZE_4+col][BC_COMP_GREEN];
                    destBlock[srcIndex+2] = (CMP_BYTE)DecData.decodedBlock[row*BLOCK_SIZE_4+col][BC_COMP_BLUE];
                    destBlock[srcIndex+3] = (CMP_BYTE)DecData.decodedBlock[row*BLOCK_SIZE_4+col][BC_COMP_ALPHA];
                    srcIndex+=4;
                }
            }

            bufferOut.WriteBlockRGBA(i*4, j*4, 4, 4, destBlock);

        }

        if (pFeedbackProc)
        {
            float fProgress = 100.f * (j * dwBlocksX) / dwBlocksXY;
            if (pFeedbackProc(fProgress, pUser1, pUser2))
            {
                return CE_Aborted;
            }
        }
    }

#ifdef USE_FILEIO_DECODE
    if (gt_File_Decode)
    {
        fprintf(gt_File_Decode, "Modes\n");
        int lf=0;
        for (int i=0; i<=CMP_MAXGTMODES; i++)
        {
            if (ModesUsed[i] =='x')
            {
                lf++;
                fprintf(gt_File_Decode,"%3d,",i);
                if (lf == 10) {
                    fprintf(gt_File_Decode,"\n");
                    lf = 0;
                }
            }
        }
        fprintf(gt_File_Decode, "\nModes Not used\n");
        lf = 0;
        for (int i = 0; i <= CMP_MAXGTMODES; i++)
        {
            if (ModesUsed[i] == '.')
            {
                lf++;
                fprintf(gt_File_Decode, "%3d,", i);
                if (lf == 10) {
                    fprintf(gt_File_Decode, "\n");
                    lf = 0;
                }
            }
        }

        fprintf(gt_File_Decode, "\nDone\n");
        fclose(gt_File_Decode);
        gt_File_Decode = NULL;
    }
#endif
******************************************/

    return CE_OK;
}

// Not implemented
CodecError CCodec_GTC::Compress_Fast(CCodecBuffer& bufferIn, CCodecBuffer& bufferOut, Codec_Feedback_Proc pFeedbackProc, CMP_DWORD_PTR pUser1, CMP_DWORD_PTR pUser2)
{
    return CE_OK;
}

// Not implemented
CodecError CCodec_GTC::Compress_SuperFast(CCodecBuffer& bufferIn, CCodecBuffer& bufferOut, Codec_Feedback_Proc pFeedbackProc, CMP_DWORD_PTR pUser1, CMP_DWORD_PTR pUser2)
{
   return CE_OK;
}
#endif
