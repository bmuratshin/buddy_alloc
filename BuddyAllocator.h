#pragma once

/*
 * Copyright (c) 2016...2021, Alex Artyushin, Boris Muratshin
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The names of the authors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <atomic>
#include <assert.h>
#include <stdint.h>

namespace qudb
{

	typedef unsigned char u_char;
	class BuddyAllocator;
	class FreeBlockHeap
	{
	public:
		FreeBlockHeap() { m_size = 0; };
		virtual ~FreeBlockHeap() {};

		void setBlockSize(unsigned int bs, BuddyAllocator *BAlloc)
		{
			parent_ = BAlloc;
			assert((m_size == 0) || (m_size == bs));
			m_size = bs;
		}
		virtual void pushBack(u_char *ptr) = 0;
		virtual void remove(u_char *ptr) = 0;
		virtual u_char *getTop() = 0;
		virtual u_char *pickTop() = 0;

	protected:
		BuddyAllocator *parent_;
		unsigned int m_size;
	};

	struct BinHeap
	{
		void init(BuddyAllocator *pba, unsigned int **pstart, int n);
		void addItem(unsigned int *uptr);
		void removeItem(unsigned int blckIdx);
		bool checkItem(unsigned int blckIdx, u_char *pBlock)
		{
			if ((blckIdx > count) || (blckIdx == 0))
				return false;
			if (pBlock != (u_char *)start[blckIdx])
				return false;
			return true;
		}
		unsigned int **start;
		unsigned int count;
		unsigned int N;
		BuddyAllocator *BAlloc;
	};
	class FreeBlockHeap48 : public FreeBlockHeap
	{
	public:
		FreeBlockHeap48() {};
		virtual ~FreeBlockHeap48() {};
		void init(BuddyAllocator *ba, unsigned int **start, int n)
		{
			binHeap.init(ba, start, n);
		}

		void pushBack(u_char *ptr) override;
		void remove(u_char *ptr) override;
		u_char *getTop() override;
		u_char *pickTop() override;

	protected:
		BinHeap binHeap;  // 4,8 bytes
	};

	struct freeBlock_t // bidirected freelist for FreeBlockHeapGen
	{
		static const int FREE = 0xfeeefeee;
		uint32_t magic_;
		uint32_t size_;
		uint32_t prevBlockIx;
		uint32_t nextBlockIx;
	};
	class FreeBlockHeapGen : public FreeBlockHeap
	{
	public:
		FreeBlockHeapGen() {};
		virtual ~FreeBlockHeapGen() {};

		void pushBack(u_char *ptr) override;
		void remove(u_char *ptr) override;
		u_char *getTop() override;
		u_char *pickTop() override;

		static bool checkBlockSize(u_char *ptr, unsigned int bsize);

	protected:
		uint32_t block2ix(const freeBlock_t *ptr);
		freeBlock_t *ix2block(uint32_t ix);

		freeBlock_t freeHead; // > 8 bytes
	};

	class BuddyAllocator
	{
		friend class FreeBlockHeapGen;
	public:
		BuddyAllocator(uint64_t maxCapacity, u_char * buf, uint64_t bufsize);
		~BuddyAllocator();

		void *alloc(unsigned int nbytes);
		void freeBlock(void *ptr);
		bool quickReset();
		int  getAllocated() const
		{
			return (int)(allockedBytes + heapAllockedBytes);
		}

	protected:
		uint32_t block2ix(const freeBlock_t *ptr) const;
		freeBlock_t *ix2block(uint32_t ix) const;

		void *alloc4();//nbytes<=4
		void *alloc8();//nbytes>4 && nbytes<=8 
		bool free4_8(u_char *ptr, int bitPos);

		void *allocOwn256();
		void *genericAlloc(uint64_t nbytes);
		void deleteFromHeap(void *ptr);
		int  getAllocedBlockSize(u_char **ppBlock);
		void setBusyFlags(u_char *newBlock, int bsize);

	protected:
		u_char *startBuf_ = NULL;
		u_char *lastAddr_ = NULL;
		unsigned int *busyFlags = NULL;

		uint64_t maxBlockSize_ = 0;
		static const int maxBlockSizes = 33;

		FreeBlockHeap *freeBlockHeap[maxBlockSizes];
		FreeBlockHeap48 heap4_;
		FreeBlockHeap48 heap8_;
		FreeBlockHeapGen gen_heaps_[maxBlockSizes - 4]; // WO 1,2,4,8

		int64_t allockedBytes;

		int64_t heapCount;
		int64_t heapAllockedBytes;
		std::atomic_flag lock_;
		bool need_free_ = true;
	};
}

