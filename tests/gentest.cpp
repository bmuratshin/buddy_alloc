#include "stdafx.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <locale.h>
#include <windows.h>


#include <thread>
#include <mutex>
#include <algorithm>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>

#include <set>
#include <map>
#include <queue>
#include <string>
#include <stack>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <atomic>

#include "../BuddyAllocator.h"
//#include "../../lib_Utils/m_thread.h"

//#include <cds/container/cuckoo_map.h>
//#include <cds/opt/hash.h>
//#include <cds/opt/options.h>

#ifndef WIN32
#define _putenv putenv
#endif 

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
//#define new DEBUG_NEW
#endif

/* ALLOCATION HOOK FUNCTION
-------------------------
An allocation hook function can have many, many different
uses. This one simply logs each allocation operation in a file.
*/
static int __cdecl MyAllocHook(
	int      nAllocType,
	void   * pvData,
	size_t   nSize,
	int      nBlockUse,
	long     lRequest,
	const unsigned char * szFileName,
	int      nLine
)
{
	//	char *operation[] = { "", "allocating", "re-allocating", "freeing" };
	//	char *blockType[] = { "Free", "Normal", "CRT", "Ignore", "Client" };

	if (nBlockUse == _CRT_BLOCK)   // Ignore internal C runtime library allocations
		return(1);

	_ASSERT((nAllocType > 0) && (nAllocType < 4));
	_ASSERT((nBlockUse >= 0) && (nBlockUse < 5));

	//	printf(
	//		"Memory operation in %s, line %d: %s a %d-byte '%s' block (# %ld)\n",
	//		szFileName, nLine, operation[nAllocType], nSize,
	//		blockType[nBlockUse], lRequest);
	//	if (pvData != NULL)
	//		printf(" at %X", pvData);

	return(1);         // Allow the memory operation to proceed
}


int
get_mtime_(void)
{
#ifdef WIN32
	struct _timeb tv;
	(void)_ftime(&tv);
	return (tv.millitm);
#else
	struct timeval tv;
	(void)gettimeofday(&tv, NULL);
	return (tv.tv_usec / 100);
#endif
}

char *err_tstamp_(void)
{
	static const char *months[] = { 
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" 
	};
	static char str[128];
	struct tm *tmp;
	time_t currt = time(NULL);

	tmp = localtime(&currt);
	sprintf(str, "[%02d/%s/%d:%02d:%02d:%02d:%03d] ", tmp->tm_mday,
		months[tmp->tm_mon], 1900 + tmp->tm_year, tmp->tm_hour,
		tmp->tm_min, tmp->tm_sec, get_mtime_());
	return str;
}


/*

extern "C" void xalloc_init()
{
	//xbd.reset(new qz::BuddyAllocator(1LL << 22));
}
extern "C" void xalloc_destroy()
{
	//xbd.reset();
}

class XallocInitDestroy
{
public:
	XallocInitDestroy();
	~XallocInitDestroy();
private:
	static int refCount;
};
int XallocInitDestroy::refCount = 0;
XallocInitDestroy::XallocInitDestroy()
{
	// Track how many static instances of XallocInitDestroy are created
	if (refCount++ == 0)
		xalloc_init();
}
XallocInitDestroy::~XallocInitDestroy()
{
	// Last static instance to have destructor called?
	if (--refCount == 0)
		xalloc_destroy();
}
static XallocInitDestroy xallocInitDestroy;
*/
extern "C" void *xalloc(size_t size);
extern "C" void xfree(void* ptr);

template <typename T>
class stl_buddy_alloc
{
public:
	typedef T                 value_type;
	typedef value_type*       pointer;
	typedef value_type&       reference;
	typedef const value_type* const_pointer;
	typedef const value_type& const_reference;
	typedef ptrdiff_t         difference_type;
	typedef size_t            size_type;
public:
//	template<class _Other>
//	struct rebind
//	{	// convert an allocator<_Ty> to an allocator <_Other>
//		typedef stl_buddy_alloc<_Other> other;
//	};
/*
	pointer address(reference _Val) const
	{	// return address of mutable _Val
		return (&_Val);
	}
	const_pointer address(const_reference _Val) const
	{	// return address of nonmutable _Val
		return (&_Val);
	}*/
	stl_buddy_alloc() throw()
	{	// construct default allocator (do nothing)
	}
	stl_buddy_alloc(const stl_buddy_alloc<T> &) throw()
	{	// construct by copying (do nothing)
	}
	template<class _Other>
	stl_buddy_alloc(const stl_buddy_alloc<_Other> &) throw()
	{	// construct from a related allocator (do nothing)
	}
//	template<class _Other>
//	stl_buddy_alloc<T>& operator=(const stl_buddy_alloc<_Other>& b)
//	{	// assign from a related allocator (do nothing)
//		return (*this);
//	}
	void deallocate(pointer _Ptr, size_type)
	{	// deallocate object at _Ptr, ignore size
		xfree(_Ptr);
	}
	pointer allocate(size_type _Count)
	{	// allocate array of _Count elements
		return (pointer)xalloc(sizeof(T) * _Count);
	}
	pointer allocate(size_type _Count, const void *)
	{	// allocate array of _Count elements, ignore hint
		return (allocate(_Count));
	}
	/*
	void construct(pointer _Ptr, const T& _Val)
	{	// construct object at _Ptr with value _Val
		::new (_Ptr) T(_Val);
	}
	void destroy(pointer _Ptr)
	{	// destroy object at _Ptr
		_Ptr; // disable warning
		_Ptr->~T();
	}
	size_t max_size() const throw()
	{	// estimate maximum array size
		size_t _Count = (size_t)(-1) / sizeof(T);
		return (0 < _Count ? _Count : 1);
	}
	*/
};

/*
// allocator TEMPLATE OPERATORS
template<class _Ty,
	class _Other, class TAllocGuard> inline
	bool operator==(const stl_buddy_alloc<_Ty>& a, const stl_buddy_alloc<_Other>& b) throw()
{	// test for allocator equality (always true)
	return a.alloc_ == b.alloc_;
}

template<class _Ty,
	class _Other, class TAllocGuard> inline
	bool operator!=(const stl_buddy_alloc<_Ty>& a, const stl_buddy_alloc<_Other>& b) throw()
{	// test for allocator inequality (always false)
	return a.alloc_ != b.alloc_;
}
*/
// FOR ALLOCATORS
template <typename T>
class q_vector : public std::vector<T, stl_buddy_alloc<T> >
{
	typedef std::vector<T, stl_buddy_alloc<T> > _Mybase;
public:
	q_vector()
	{
	}

	explicit q_vector(size_t _Count)
		: _Mybase(_Count)
	{
	}
	q_vector(size_t _Count, const T& _Val)
		: _Mybase(_Count, _Val)
	{
	}
	template<class _Iter>
	q_vector(_Iter _First, _Iter _Last)
		: _Mybase(_First, _Last)
	{	// construct from [_First, _Last)
	}
};

typedef std::basic_string<char, std::char_traits<char>, stl_buddy_alloc<char> > q_string;
typedef std::basic_string<wchar_t, std::char_traits<wchar_t>, stl_buddy_alloc<wchar_t> > q_wstring;


typedef std::basic_stringstream<char, std::char_traits<char>,
	stl_buddy_alloc<char> > q_stringstream;
typedef std::basic_ostringstream<char, std::char_traits<char>,
	stl_buddy_alloc<char> > q_ostringstream;

typedef std::basic_stringstream<wchar_t, std::char_traits<wchar_t>,
	stl_buddy_alloc<wchar_t> > q_wstringstream;
typedef std::basic_ostringstream<wchar_t, std::char_traits<wchar_t>,
	stl_buddy_alloc<wchar_t> > q_wostringstream;

template <typename T>
class q_list : public std::list<T, stl_buddy_alloc<T> >
{
	typedef std::list<T, stl_buddy_alloc<T> > _Mybase;
public:
	q_list()
	{
	}
	template<class _Iter>
	q_list(_Iter _First, _Iter _Last)
		: _Mybase(_First, _Last)
	{	// construct from [_First, _Last)
	}
};

template <typename T>
class q_deque : public std::deque<T, stl_buddy_alloc<T> >
{ };

template <typename T>
class q_stack : public std::stack<T, q_deque<T> >
{ };

template <typename T>
class q_queue : public std::queue<T, q_deque<T> >
{ };

template <typename _Kty, typename _Ty>
class q_map : public std::map<_Kty, _Ty, std::less<const _Kty>, stl_buddy_alloc<std::pair<const _Kty, _Ty> > >
{ };

template <typename _Kty, typename _Ty, typename Comp>
class q_map_ex : public std::map<_Kty, _Ty, Comp, stl_buddy_alloc<std::pair<_Kty, _Ty>> >
{ };

template <typename _Kty>
class q_set : public std::set<_Kty, std::less<_Kty>, stl_buddy_alloc<_Kty> >
{ };

template <typename _Kty>
class q_multiset : public std::multiset<_Kty, std::less<_Kty>, stl_buddy_alloc<_Kty> >
{};


//------------------------------------------------------------------
#define BUF_SIZE 256
wchar_t szName[] = L"Local\\QortGlobalData";
wchar_t szMxName[] = L"Local\\QortGlobalMx";

std::map<std::string, std::string> smap;
std::mutex smx;
std::atomic_flag slock;
void s_tmp_proc(int curid)
{
	for (int i = 0; i < 100000000; i++)
	{

		char buf1[64];
		sprintf(buf1, "key_%d_%d", curid, (i % 100) + 1);
		char buf2[64];
		sprintf(buf2, "val%d", i + 1);
		{
			//mcrit_guard_t(smx);
			while (slock.test_and_set(std::memory_order_acquire));  // acquire lock
			smap.erase(buf1);
			smap[buf1] = buf2;
			slock.clear(std::memory_order_release);                 // release lock
		}

		if (i && 0 == (i % 1000000))
			printf("%c", curid + 'a');
	}
}


struct glob_header_t {
	// каждый знает что такое magic
	uint64_t magic_;
	// hint для присоединения к разделяемой памяти
	const void *own_addr_;
	// собственно аллокатор
	qudb::BuddyAllocator alloc_;
	// спинлок
	std::atomic_flag lock_;
	// контейнер для тестирования
	q_map<q_string, q_string> q_map_;

	static const size_t alloc_shift = 0x01000000;
	static const size_t balloc_size = 0x10000000;
	static const size_t alloc_size = balloc_size + alloc_shift;
	static glob_header_t *pglob_;
};
static_assert (sizeof(glob_header_t) < glob_header_t::alloc_shift, "glob_header_t size mismatch");
glob_header_t *glob_header_t::pglob_ = NULL;

void s_tmp2_proc(int curid)
{
	q_map<q_string, q_string> *pqmap = &glob_header_t::pglob_->q_map_;
	q_map<q_string, q_string> &qmap = *pqmap;

	const size_t qsz = 1024;
	void * q_ueue[qsz];
	size_t wr_pos = 0;
	size_t rd_pos = 0;
#if 1
	for (int i = 0; !i || wr_pos != rd_pos; i++)
	{

		if (i < 100000000) {
			int sz = 4;// rand() % 1024;
			void *p = xalloc(sz);
			q_ueue[wr_pos] = p;
			wr_pos = (wr_pos + 1) % qsz;
		}

		if (i > 1000) {
			void *p = q_ueue[rd_pos];
			xfree(p);

			q_ueue[rd_pos] = NULL;
			rd_pos = (rd_pos + 1) % qsz;

		}

		if (i && 0 == (i % 1000000))
			printf(".");

	}
#else
	for (int i = 0; i < 100000000; i++) //!i || wr_pos != rd_pos; i++)
	{
		int sz = 4;// rand() % 1024;
		void *p = xalloc(sz);
		xfree(p);

		if (i && 0 == (i % 1000000))
			printf(".");

	}
#endif
	qmap.clear();
}



static uint64_t xalloced = 0;
extern "C" void *xalloc(size_t size)
{
	xalloced++;
	void* clientsMemoryPtr = glob_header_t::pglob_->alloc_.alloc((unsigned)size);
	return clientsMemoryPtr;
}

static uint64_t xfreed = 0;
extern "C" void xfree(void* ptr)
{
	xfreed++;
	if (ptr == 0)
		return;

	glob_header_t::pglob_->alloc_.freeBlock(ptr);
}


int main(int argc, wchar_t* argv[])
{
	{
		//my_cuckoo_map map;
	}
	if(0)
	{
		if (argc < 2)
		{
			printf("Use xxx mathreads\n");
			exit(1);
		}
		int maxid = _wtoi(argv[1]);

		fprintf(stderr, "\nINFO: start %s\n", err_tstamp_());

		std::vector<std::thread> threads(maxid);
		for (int i = 0; i < maxid; i++)
		{
			threads[i] = std::thread(s_tmp_proc, i);
		}
		for (std::thread &thr : threads)
		{
			thr.join();
		}
		threads.clear();

		fprintf(stderr, "\nINFO: finish %s\n", err_tstamp_());
		exit(0);
	}

	_CrtMemState checkPt1;
	_CrtMemCheckpoint(&checkPt1);
	_CrtSetAllocHook(MyAllocHook);
	assert(_CrtCheckMemory());

	if (argc < 3)
	{
		printf("Use xxx maxid, curid\n");
		exit(1);
	}

	int maxid = _wtoi(argv[1]);
	int curid = _wtoi(argv[2]);
	if (maxid <= 0 || maxid > 100)
	{
		printf("1 <= maxid <= 100U\n");
		exit(1);
	}
	if (curid < 0 || curid >= maxid)
	{
		printf("curid >= 0 && curid < maxid\n");
		exit(1);
	}


	{
		//HANDLE hMutex = NULL; 
		HANDLE hMapFile = NULL;
		if (0 == curid)
		{
			hMapFile = CreateFileMapping(
				INVALID_HANDLE_VALUE,    // use paging file
				NULL,                    // default security
				PAGE_READWRITE,          // read/write access
				(glob_header_t::alloc_size >> 32),                       // maximum object size (high-order DWORD)
				(glob_header_t::alloc_size & 0xffffffff),                       // maximum object size (low-order DWORD)
				szName);

			//hMutex = CreateMutex(NULL, FALSE, szMxName);
		}
		else
		{
			hMapFile = OpenFileMapping(
				FILE_MAP_ALL_ACCESS,   // read/write access
				FALSE,                 // do not inherit the name
				szName);               // name of mapping object

			//hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, szMxName);

		}
		void *hint = (void *)0x200000000000ll;
		unsigned char *shared_ptr = NULL;
		if (hMapFile)
		{
			shared_ptr = (unsigned char*)MapViewOfFileEx(
				hMapFile,   // handle to map object
				FILE_MAP_ALL_ACCESS, // read/write permission
				0,
				0,
				glob_header_t::alloc_size,
				hint);
		}

		if (NULL == hMapFile || shared_ptr != hint)
		{
			fprintf(stderr, "\nERR: can't map memory %s\n", err_tstamp_());
			exit(1);
		}
		glob_header_t::pglob_ = (glob_header_t *)shared_ptr;
		q_map<q_string, q_string> *pqmap = &glob_header_t::pglob_->q_map_;

		if (0 == curid)
		{
			new (&glob_header_t::pglob_->alloc_) qudb::BuddyAllocator(glob_header_t::balloc_size, shared_ptr + glob_header_t::alloc_shift, glob_header_t::alloc_size);
			//pqmap = (q_map<q_string, q_string>*)glob_header_t::pglob_->alloc_.allocBlock(sizeof(q_map<q_string, q_string>));
			new (&glob_header_t::pglob_->q_map_) q_map<q_string, q_string>();
			glob_header_t::pglob_->lock_.clear();
			//glob_header_t::pglob_->q_map_ = pqmap;
			//printf("<%p>\n", pqmap);
			fprintf(stderr, "\nINFO: press a key %s\n", err_tstamp_());
			fgetc(stdin);
		}
		if (0)
		{
			fprintf(stderr, "\nINFO: start %s\n", err_tstamp_());
			std::vector<std::thread> threads(8);
			for (int i = 0; i < 4; i++)
			{
				threads[i] = std::thread(s_tmp2_proc, i);
			}
			for (std::thread &thr : threads)
			{
				if (thr.joinable())
					thr.join();
			}
			threads.clear();
			fprintf(stderr, "\nINFO: stop %s\n", err_tstamp_());
			return 0;
		}


		fprintf(stderr, "\nINFO: press a key %s\n", err_tstamp_());
		fgetc(stdin);


		fprintf(stderr, "\nINFO: start %s\n", err_tstamp_());

		{
			q_map<q_string, q_string> &qmap = *pqmap;
			for (int i = 0; i < 100000000; i++)
			{
				char buf1[64];
				sprintf(buf1, "key_%d_%d", curid, (i % 100) + 1);
				char buf2[64];
				sprintf(buf2, "val_%d", i + 1);

				while (glob_header_t::pglob_->lock_.test_and_set(std::memory_order_acquire));  // acquire lock

				qmap.erase(buf1); // пусть аллокатор трудится
				qmap[buf1] = buf2;

				glob_header_t::pglob_->lock_.clear(std::memory_order_release);                 // release lock
			}
#if 0
			//std::map<std::string, std::string> qmap;
			const size_t qsz = 1024*4;
			void * q_ueue[qsz];
			size_t wr_pos = 0;
			size_t rd_pos = 0;
			for (int i = 0; !i || wr_pos != rd_pos; i++)
			{

//				char buf1[64];
//				sprintf(buf1, "key_%d_%d", curid, (i % 100) + 1);
//				char buf2[64];
//				sprintf(buf2, "val%d", i + 1);

/*				{
					DWORD dwCount = 0, dwWaitResult;
					while (dwCount < 20)
					{
						dwWaitResult = WaitForSingleObject(
							hMutex,    // handle to mutex
							INFINITE);  // no time-out interval

						switch (dwWaitResult)
						{
						case WAIT_OBJECT_0:
							dwCount = 200;
							break;

							// The thread got ownership of an abandoned mutex
							// The database is in an indeterminate state
						case WAIT_ABANDONED:
							return FALSE;
						}
					}
				}
				//auto dwWaitResult = WaitForSingleObject(
				//	hMutex,    // handle to mutex
				//	INFINITE);  // no time-out interval
*/
				//while (glob_header_t::pglob_->lock_.test_and_set(std::memory_order_acquire));  // acquire lock

				if (i < 100000000) {
					int sz = 4;// rand() % 1024;
					void *p = xalloc(sz);
					q_ueue[wr_pos] = p;
					wr_pos = (wr_pos + 1) % qsz;
				}

				if (i > 1000) {
					void *p = q_ueue[rd_pos];
					rd_pos = (rd_pos + 1) % qsz;

					xfree(p);
				}
//				qmap.erase(buf1);
//				qmap[buf1] = buf2;

				//ReleaseMutex(hMutex);
				//glob_header_t::pglob_->lock_.clear(std::memory_order_release);                 // release lock

				if (i && 0 == (i % 1000000))
					printf(".");

			}
#endif
			for (const auto &it : qmap)
			{
				printf("%s=%s\n", it.first.c_str(), it.second.c_str());
			}
			qmap.clear();
		}

		fprintf(stderr, "\nINFO: finish %lld %lld %s\n", xalloced, xfreed, err_tstamp_());

		//fprintf(stderr, "\nINFO: press a key %s\n", err_tstamp_());
		//fgetc(stdin);

		UnmapViewOfFile(shared_ptr);
		//if (hMutex)
			//CloseHandle(hMutex);
		if (hMapFile)
			CloseHandle(hMapFile);
		hMapFile = NULL;

	}

	assert(_CrtCheckMemory());
	_CrtMemDumpAllObjectsSince(&checkPt1);
	_CrtMemCheckpoint(&checkPt1);

	return 0;
}

