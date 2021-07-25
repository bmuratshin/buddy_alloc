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

//#include "stdafx.h"
#include "BuddyAllocator.h"

namespace qudb
{

	struct spin_lock_t {
		spin_lock_t(std::atomic_flag &lk) : lock_(lk)
		{
			while (lock_.test_and_set(std::memory_order_acquire));  // acquire lock
		}
		~spin_lock_t()
		{
			lock_.clear(std::memory_order_release);                 // release lock
		}

		std::atomic_flag &lock_;
	};

	void* new_impl(int64_t nbytes)
	{
		return malloc(nbytes);
	}

	void free_impl(void* buf)
	{
		free(buf);
	}

	static unsigned int BitCount[] = { 0,1,2,2, 3,3,3,3, 4,4,4,4, 4,4,4,4 };
	static unsigned int FreeBit[] = { 3,3,3,3, 3,3,3,3, 2,2,2,2, 1,1,0,32 };
	static unsigned int getBSize(uint64_t nbytes)
	{
		assert(nbytes > 8);
		nbytes--;
		int retval = 0;
		if (nbytes >= 0x100000000)
		{
			retval += 32;
			nbytes >>= 32;
		}
		if (nbytes >= 0x10000)
		{
			retval += 16;
			nbytes >>= 16;
		}
		if (nbytes >= 0x100)
		{
			retval += 8;
			nbytes >>= 8;
		}
		if (nbytes >= 0x10)
		{
			retval += 4;
			nbytes >>= 4;
		}
		return retval + BitCount[nbytes];
	}
	static unsigned int getFreeBit(unsigned int busyFlags)
	{
		assert(busyFlags != 0xffffffff);
		int bitPos = 0;
		if (busyFlags < 0xffff0000)
		{
			busyFlags >>= 16;
			bitPos = 16;
		}
		else
			busyFlags ^= 0xffff0000;
		if (busyFlags < 0xff00)
		{
			busyFlags >>= 8;
			bitPos += 8;
		}
		else
			busyFlags ^= 0xff00;
		if (busyFlags < 0xf0)
		{
			busyFlags >>= 4;
			bitPos += 4;
		}
		else
			busyFlags ^= 0xf0;
		return bitPos + FreeBit[busyFlags];
	}

	void *BuddyAllocator::allocOwn256()
	{
		const unsigned int bsize = 8;
		assert(maxBlockSize_ >= bsize);
		u_char *newBlock = freeBlockHeap[bsize]->getTop();
		if (newBlock == NULL)
		{
			unsigned int bigSize = bsize + 1;
			for (; bigSize <= maxBlockSize_; bigSize++)
			{
				newBlock = freeBlockHeap[bigSize]->getTop();
				if (newBlock)
					break;
			}
			if (!newBlock)
				return NULL;

			do
			{
				//split
				bigSize--;
				int blckSize = 1 << bigSize;
				freeBlockHeap[bigSize]->pushBack(newBlock + blckSize);
			} while (bigSize > bsize);
		}
		allockedBytes += 256;
		setBusyFlags(newBlock, bsize);
		return newBlock;
	}
	void *BuddyAllocator::alloc(unsigned int nbytes)
	{
		spin_lock_t lk(lock_);

		if (nbytes <= 8)
		{
			if (nbytes <= 4)
			{
				nbytes = 4;
				return alloc4();
			}
			nbytes = 8;
			return alloc8();
		}
		unsigned int bsize = getBSize(nbytes);

		if (maxBlockSize_ < bsize)
			return genericAlloc(nbytes);

		u_char *newBlock = freeBlockHeap[bsize]->getTop();
		if (newBlock == NULL)
		{
			unsigned int bigSize = bsize + 1;
			for (; bigSize <= maxBlockSize_; bigSize++)
			{
				newBlock = freeBlockHeap[bigSize]->getTop();
				if (newBlock)
					break;
			}
			if (!newBlock)
				return genericAlloc(nbytes);

			do
			{
				//split
				bigSize--;
				uint64_t blckSize = 1ll << bigSize;
				freeBlockHeap[bigSize]->pushBack(newBlock + blckSize);
			} while (bigSize > bsize);
		}
		allockedBytes += (1LL << bsize);
		setBusyFlags(newBlock, bsize);
		return newBlock;
	}
	void BuddyAllocator::freeBlock(void *ptr)
	{
		spin_lock_t lk(lock_);

		u_char *pBlock = (u_char *)ptr;
		if ((pBlock < startBuf_) || (pBlock >= lastAddr_))
		{
			deleteFromHeap(pBlock);
			return;
		}
		unsigned int bsize = getAllocedBlockSize(&pBlock);
		if (bsize <= 0)
		{//4 or 8 byte or error
			if (bsize < 0)//error
				return;
			return;
		}

		//mergeFreeBlocks
		int bytePos = (int)(pBlock - startBuf_);
		unsigned int addrBit = 1 << bsize;
		allockedBytes -= addrBit;

		unsigned int curFlag = busyFlags[bytePos >> 8];
		unsigned int BusyBit = 1 << ((bytePos >> 3) & 31);
		int buddyShift = 1 << (bsize - 3);

		while (bsize < 8)
		{
			if (bytePos&addrBit)
			{
				BusyBit >>= buddyShift;
				u_char *buddyBlock = pBlock - addrBit;
				if ((curFlag&BusyBit) || !FreeBlockHeapGen::checkBlockSize(buddyBlock, bsize))
				{//buddy is busy
					freeBlockHeap[bsize]->pushBack(pBlock);
					return;
				}
				bytePos ^= addrBit;
				pBlock = buddyBlock;
				freeBlockHeap[bsize]->remove(pBlock);
			}
			else
			{
				u_char *buddyBlock = pBlock + addrBit;
				if ((curFlag&(BusyBit << buddyShift)) || !FreeBlockHeapGen::checkBlockSize(buddyBlock, bsize))
				{//buddy is busy
					freeBlockHeap[bsize]->pushBack(pBlock);
					return;
				}
				freeBlockHeap[bsize]->remove(buddyBlock);
			}
			bsize++;
			addrBit <<= 1;
			buddyShift <<= 1;
		}

		int flagStep = addrBit >> 8;
		unsigned int *pcurFlag = busyFlags + (bytePos >> 8);
		while (bsize < maxBlockSize_)
		{
			//findBuddy
			if (bytePos&addrBit)
			{
				pcurFlag -= flagStep;
				u_char *buddyBlock = pBlock - addrBit;
				if (((*pcurFlag) & 1) || !FreeBlockHeapGen::checkBlockSize(buddyBlock, bsize))//buddy is busy
					break;
				bytePos ^= addrBit;
				pBlock = buddyBlock;
				freeBlockHeap[bsize]->remove(pBlock);
			}
			else
			{
				u_char *buddyBlock = pBlock + addrBit;
				if ((pcurFlag[flagStep] & 1) || !FreeBlockHeapGen::checkBlockSize(buddyBlock, bsize))//buddy is busy
					break;
				freeBlockHeap[bsize]->remove(buddyBlock);
			}
			bsize++;
			addrBit <<= 1;
			flagStep <<= 1;
		}
		freeBlockHeap[bsize]->pushBack(pBlock);
	}

	static unsigned int MASK2[] = { 0xfffffffc,0xfffffff3,0xffffffcf,0xffffff3f, 0xfffffcff,0xfffff3ff,0xffffcfff,0xffff3fff,
								    0xfffcffff,0xfff3ffff,0xffcfffff,0xff3fffff, 0xfcffffff,0xf3ffffff,0xcfffffff,0x3fffffff };
	static unsigned int MASK4[] = { 0xfffffff0,0xffffff0f,0xfffff0ff,0xffff0fff, 0xfff0ffff,0xff0fffff,0xf0ffffff,0x0fffffff };

	int BuddyAllocator::getAllocedBlockSize(u_char **ppBlock)
	{
		int pos = (int)(*ppBlock - startBuf_);
		assert(pos >= 0);
		assert((pos & 3) == 0);
		unsigned int *curFlags = busyFlags + (pos >> 8);
		int bitPos = (pos >> 3) & 31;
		unsigned int Bit = 1 << bitPos;
		if (((*curFlags)&Bit) == 0)
		{
			//4 or 8 bytes
			u_char *pBlock256 = startBuf_ + (pos & 0x7fffff00);
			if (free4_8(pBlock256, pos))//all done
				return 0;
			pos = (int)(pBlock256 - startBuf_);
			busyFlags[pos >> 8] = 0;//clear busyBit
			*ppBlock = pBlock256;
			return 8;
		}
		assert((pos & 0xf) == 0);
		Bit <<= 1;
		if ((*curFlags)&Bit)
		{
			(*curFlags) &= MASK2[bitPos >> 1];
			return 4;
		}
		assert((pos & 0x1f) == 0);
		int shift = bitPos + 2;
		unsigned int val = (*curFlags)&(3 << shift);
		if (val)
		{
			val >>= shift;
			(*curFlags) &= MASK4[bitPos >> 2];
			return 4 + val;
		}
		assert(bitPos == 0);
		unsigned int retval = 8 + ((*curFlags) >> 8);
		*curFlags = 0;
		assert(retval < 32);
		return retval;
	}
	void BuddyAllocator::setBusyFlags(u_char *pBlock, int bsize)
	{
		assert(bsize > 3);
		int pos = (int)(pBlock - startBuf_);
		assert(pos >= 0);
		unsigned int *curFlags = busyFlags + (pos >> 8);
		int bitPos = (pos >> 3) & 31;
		unsigned int Bit = 1 << bitPos;
		assert(((*curFlags)&Bit) == 0);
		(*curFlags) |= Bit;
		if (bsize == 8)
			return;
		assert(Bit < 0x80000000);
		Bit <<= 1;
		assert(((*curFlags)&Bit) == 0);
		if (bsize == 4)
		{
			(*curFlags) |= Bit;
			return;
		}
		assert(Bit < 0x40000000);
		int shift = bitPos + 2;
		assert(((*curFlags)&(3 << shift)) == 0);
		if (bsize < 8)
		{
			(*curFlags) |= (bsize - 4) << shift;
			return;
		}
		assert(bitPos == 0);
		assert((*curFlags) == 1);
		*curFlags |= ((bsize - 8) << 8);
	}
	bool BuddyAllocator::free4_8(u_char *pBlock256, int pos)
	{
		unsigned int *pBlock = (unsigned int *)pBlock256;
		unsigned int val = *pBlock;

		if (val & 1)
		{//4
			if (((*pBlock) == 0xffffffff) && (pBlock[2] == 0xffffffff))
				heap4_.pushBack(pBlock256);

			assert((pos & 3) == 0);

			unsigned int busyBit = ((pos & 255) >> 2);
			if (busyBit > 31)
			{
				busyBit -= 31;
				assert((pBlock[2] & (1 << busyBit)) != 0);
				pBlock[2] ^= (1 << busyBit);
			}
			else
			{
				assert((pBlock[0] & (1 << busyBit)) != 0);
				(*pBlock) ^= (1 << busyBit);
			}
			if (pBlock[0] != 7)
				return true;
			if (pBlock[2] != 0)
				return true;
			heap4_.remove(pBlock256);
		}
		else
		{//8
			if ((*pBlock) == 0xfffffffe)
				heap8_.pushBack(pBlock256);
			assert((pos & 7) == 0);
			unsigned int busyBit = 1 << ((pos & 255) >> 3);
			assert(((*pBlock)&busyBit) != 0);
			(*pBlock) ^= busyBit;
			if (*pBlock)
				return true;
			heap8_.remove(pBlock256);
		}
		//bigBlock is empty - free it
		if ((pBlock256 < startBuf_) || (pBlock256 >= lastAddr_))
		{
			deleteFromHeap(pBlock256);
			return true;
		}
		//bigBlock should be deleted 
		return false;
	}
	void *BuddyAllocator::alloc8()
	{
		u_char *pBlock256 = heap8_.pickTop();
		if (!pBlock256)
		{
			pBlock256 = (u_char *)allocOwn256();
			if (!pBlock256)
				return new_impl(8);

			unsigned int *pBlock = (unsigned int *)pBlock256;
			pBlock[0] = 0;
			heap8_.pushBack(pBlock256);
		}
		unsigned int *pBlock = (unsigned int *)pBlock256;
		assert((*pBlock) != 0xfffffffe);
		unsigned int freeBit = getFreeBit(*pBlock);
		assert(freeBit != 0);
		assert(freeBit < 32);
		(*pBlock) |= 1 << freeBit;
		if ((*pBlock) == 0xfffffffe)
		{
			heap8_.remove(pBlock256);
		}
		return pBlock256 + (freeBit << 3);
	}

	void *BuddyAllocator::alloc4()
	{
		u_char *pBlock256 = heap4_.pickTop();
		if (!pBlock256)
		{
			pBlock256 = (u_char *)allocOwn256();
			if (!pBlock256)
				return new_impl(4);

			heap4_.pushBack(pBlock256);
			unsigned int *pBlock = (unsigned int *)pBlock256;
			pBlock[0] = 7;
			pBlock[2] = 0;
		}
		unsigned int *pBlock = (unsigned int *)pBlock256;
		unsigned int freeBit;
		if ((*pBlock) != 0xffffffff)
		{
			freeBit = getFreeBit(*pBlock);
			assert(freeBit > 2);
			assert(freeBit < 32);
			(*pBlock) |= 1 << freeBit;
			if ((*pBlock) != 0xffffffff)
				return pBlock256 + (freeBit << 2);
		}
		else
		{
			assert(pBlock[2] != 0xffffffff);
			freeBit = getFreeBit(pBlock[2]);
			assert(freeBit < 32);
			pBlock[2] |= 1 << freeBit;
			freeBit += 32;
		}
		if (pBlock[2] == 0xffffffff)
			heap4_.remove(pBlock256);
		return pBlock256 + (freeBit << 2);
	}

	void *BuddyAllocator::genericAlloc(uint64_t nbytes)
	{
		heapCount++;
		heapAllockedBytes += nbytes;
		return new_impl(nbytes);
	}
	void BuddyAllocator::deleteFromHeap(void *ptr)
	{
#ifdef _DEBUG
		heapCount--;
		int *iPtr = (int *)ptr;
		heapAllockedBytes -= iPtr[-4];
#endif
		free_impl(ptr);
	}
	bool BuddyAllocator::quickReset()
	{
		spin_lock_t lk(lock_);

		if (heapCount != 0)
			return false;
		heapCount = allockedBytes = heapAllockedBytes = 0;
		for (int ii = 2; ii < 32; ii++)
			freeBlockHeap[ii]->setBlockSize(ii, this);
		int64_t bufSize = 1LL << (1 + maxBlockSize_);
		memset(busyFlags, 0, bufSize / 64);
		freeBlockHeap[maxBlockSize_]->pushBack(startBuf_ + bufSize / 2);
		freeBlockHeap[maxBlockSize_]->pushBack(startBuf_);
		unsigned int **start3 = (unsigned int **)alloc(16);
		heap8_.init(this, start3, 4);
		unsigned int **start2 = (unsigned int **)alloc8();
		heap4_.init(this, start2, 2);
		return true;
	}

	BuddyAllocator::BuddyAllocator(uint64_t maxCapacity, u_char *buf, uint64_t bufsize)
	{
		lock_.clear();

		heapCount = allockedBytes = heapAllockedBytes = 0;

		freeBlockHeap[0] = freeBlockHeap[1] = NULL;
		freeBlockHeap[2] = &heap4_;
		freeBlockHeap[3] = &heap8_;
		int ix = 0;
		for (int i = 4; i < maxBlockSizes; i++)
			freeBlockHeap[i] = &gen_heaps_[ix++];
		// since size of 4
		for (int ii = 2; ii < maxBlockSizes; ii++)
			freeBlockHeap[ii]->setBlockSize(ii, this);
		int bsize = getBSize(maxCapacity);
		startBuf_ = 0;
		uint64_t bufSize = 1LL << bsize;
		uint64_t totSize;
		uint64_t reqSize = bufSize + (bufSize / 64);

		if (buf && reqSize <= bufsize)
		{
			totSize = reqSize;
			startBuf_ = buf;
			need_free_ = false;
			bsize--;
		}
		else
		{
			need_free_ = true;
			do
			{
				bufSize = 1LL << bsize;
				totSize = bufSize + (bufSize / 64);
				startBuf_ = (u_char*)new_impl(totSize);
				bsize--;
			} while (startBuf_ == NULL);
		}
		busyFlags = (unsigned int *)startBuf_;
		startBuf_ += bufSize / 64;
		lastAddr_ = startBuf_ + bufSize;
		memset(busyFlags, 0, bufSize / 64);
		maxBlockSize_ = bsize;
		assert((bufSize / 2) == (1LL << maxBlockSize_));
		freeBlockHeap[maxBlockSize_]->pushBack(startBuf_ + bufSize / 2);
		freeBlockHeap[maxBlockSize_]->pushBack(startBuf_);
		unsigned int **start3 = (unsigned int **)alloc(16);
		heap8_.init(this, start3, 4);
		unsigned int **start2 = (unsigned int **)alloc8();
		heap4_.init(this, start2, 2);
	}
	BuddyAllocator::~BuddyAllocator()
	{
		spin_lock_t lk(lock_);

		if (need_free_ && busyFlags)
			free_impl(busyFlags);
	}

	uint32_t BuddyAllocator::block2ix(const freeBlock_t *ptr) const
	{
		const u_char *pBlock = (const u_char *)ptr;
		assert((pBlock >= startBuf_) && (pBlock < lastAddr_));
		int64_t bytePos = 1 + ((pBlock - startBuf_) >> 3);  // > 0
		assert(bytePos == (bytePos & 0xffffffff));
		return (uint32_t)bytePos;
	}

	freeBlock_t *BuddyAllocator::ix2block(uint32_t ix) const
	{
		assert(ix > 0);
		ix--;
		assert((ix << 3) < (lastAddr_ - startBuf_));
		return (freeBlock_t *)(startBuf_ + (ix << 3));
	}

	// ------ BuddyAllocator ----------------------------------------------------------------
	uint32_t FreeBlockHeapGen::block2ix(const freeBlock_t *ptr)  
	{ 
		if (ptr == &freeHead)
			return 0;
		return parent_->block2ix(ptr);
	}

	freeBlock_t *FreeBlockHeapGen::ix2block(uint32_t ix)  
	{ 
		if (0 == ix)
			return &freeHead;
		return parent_->ix2block(ix);
	};

	void FreeBlockHeapGen::pushBack(u_char *ptr)
	{
		freeBlock_t *pBlock = (freeBlock_t *)(ptr);
		pBlock->magic_ = freeBlock_t::FREE;
		pBlock->size_ = m_size;
		uint32_t block_ix = block2ix(pBlock);

		pBlock->nextBlockIx = freeHead.nextBlockIx;
		freeBlock_t *pNextBlock = ix2block(freeHead.nextBlockIx);
		pNextBlock->prevBlockIx = block_ix;

		pBlock->prevBlockIx = 0;
		freeHead.nextBlockIx = block_ix;
	}
	void FreeBlockHeap48::pushBack(u_char *ptr)
	{
		uint32_t *uptr = (uint32_t *)ptr;
		binHeap.addItem(uptr + 1);
	}

	void FreeBlockHeapGen::remove(u_char *ptr)
	{
		freeBlock_t *pBlock = (freeBlock_t *)(ptr);
		assert(pBlock->magic_ == freeBlock_t::FREE);
		assert(pBlock->size_ == m_size);

		uint32_t previx = pBlock->prevBlockIx;
		freeBlock_t *prev = ix2block(previx);

		uint32_t nextix = pBlock->nextBlockIx;
		freeBlock_t *next = ix2block(nextix);

		prev->nextBlockIx = nextix;
		next->prevBlockIx = previx;
	}
	void FreeBlockHeap48::remove(u_char *ptr)
	{
		uint32_t *uptr = (uint32_t *)ptr;
		binHeap.removeItem(uptr[1]);
	}

	u_char *FreeBlockHeapGen::pickTop()
	{
		if (0 == freeHead.nextBlockIx)
			return NULL;
		freeBlock_t *pBlock = ix2block(freeHead.nextBlockIx);
		assert(pBlock->magic_ == freeBlock_t::FREE);
		assert(pBlock->size_ == m_size);
		return (u_char *)pBlock;
	}
	u_char *FreeBlockHeap48::pickTop()
	{
		if (binHeap.count == 0)
			return NULL;
		unsigned int *uptr = binHeap.start[1];
		return (u_char *)(uptr - 1);
	}

	u_char *FreeBlockHeapGen::getTop()
	{
		if (0 == freeHead.nextBlockIx)
			return NULL;
		freeBlock_t *pBlock = ix2block(freeHead.nextBlockIx);

		assert(pBlock->magic_ == freeBlock_t::FREE);
		assert(pBlock->size_ == m_size);
		uint32_t nextix = pBlock->nextBlockIx;
		freeBlock_t *next = ix2block(nextix);
		freeHead.nextBlockIx = nextix;
		next->prevBlockIx = 0;
		return (u_char *)pBlock;
	}
	u_char *FreeBlockHeap48::getTop()
	{
		if (binHeap.count == 0)
			return NULL;
		unsigned int *uptr = binHeap.start[1];
		binHeap.removeItem(1);
		return (u_char *)(uptr - 1);
	}
	bool FreeBlockHeapGen::checkBlockSize(u_char *ptr, unsigned int bsize)
	{
		freeBlock_t *pBlock = (freeBlock_t *)ptr;
		assert(pBlock->magic_ == freeBlock_t::FREE);
		return (pBlock->size_ == bsize);
	}

	//---------- BinHeap -----------------------------------------------------------------------------------------------------------------------

	void BinHeap::init(BuddyAllocator *ba, unsigned int **pstart, int n)
	{
		BAlloc = ba;
		start = pstart;
		start[0] = 0;
		count = 0;
		assert((n&(n - 1)) == 0);
		assert(n > 1);
		N = n;

	}
	void BinHeap::addItem(unsigned int *uptr)
	{
		count++;
		if (count >= N)
		{
			assert(count == N);
			N <<= 1;
			unsigned int **tmpBuf = (unsigned int **)BAlloc->alloc(N * sizeof(unsigned int *));
			memcpy(tmpBuf, start, count * sizeof(unsigned int *));
			BAlloc->freeBlock(start);
			start = tmpBuf;
		}
		unsigned int pos = count;
		unsigned int *pParent = start[pos >> 1];
		while (pParent > uptr)
		{
			*pParent = pos;
			start[pos] = pParent;
			pos >>= 1;
			pParent = start[pos >> 1];
		}
		start[pos] = uptr;
		*uptr = pos;
	}

	void BinHeap::removeItem(unsigned int blckIdx)
	{
		if (blckIdx >= count)
		{
			assert(blckIdx == count);
			count--;
			return;
		}
		unsigned int *uptr = start[count];
		count--;
		unsigned int *pParent = start[blckIdx >> 1];
		if (pParent > uptr)
		{
			do
			{
				*pParent = blckIdx;
				start[blckIdx] = pParent;
				blckIdx >>= 1;
				pParent = start[blckIdx >> 1];
			} while (pParent > uptr);
		}
		else
		{
			unsigned int childIdx = (blckIdx << 1);
			while (childIdx < count)
			{
				if (start[childIdx] > start[childIdx + 1])
					childIdx++;
				if (start[childIdx] > uptr)
					goto POS_FOUND;
				*start[childIdx] = blckIdx;
				start[blckIdx] = start[childIdx];
				blckIdx = childIdx;
				childIdx <<= 1;
			}
			if (childIdx == count)
			{
				if (start[childIdx] < uptr)
				{
					*start[childIdx] = blckIdx;
					start[blckIdx] = start[childIdx];
					blckIdx = childIdx;
				}
			}
		}
	POS_FOUND:
		start[blckIdx] = uptr;
		*uptr = blckIdx;
	}

}
