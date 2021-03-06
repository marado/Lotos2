#if defined __EXCEPTION_TRACER__

#ifndef LOTOS2_EXCEPTION_H
#define	LOTOS2_EXCEPTION_H


namespace lotos2 {
	
class ExceptionHandler
{
public:
	ExceptionHandler();
	~ExceptionHandler();
	bool InstallHandler();
	bool RemoveHandler();
	static void dumpStack();
private:
#if defined WIN32 || defined __WINDOWS__
#if defined _MSC_VER || defined __USE_MINIDUMP__

	static long __stdcall MiniDumpExceptionHandler(struct _EXCEPTION_POINTERS *pExceptionInfo);
	static int ref_counter;

#elif __GNUC__

	struct SEHChain {
		SEHChain *prev;
		void *SEHfunction;
		};
	SEHChain chain;
#endif
#endif

	bool isInstalled;
};

} // namespace lotos2

#endif /* LOTOS2_EXCEPTION_H */

#endif /* __EXCEPTION_TRACER__ */
