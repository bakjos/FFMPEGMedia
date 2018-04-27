#include "LambdaRunnable.h"


LambdaRunnable::LambdaRunnable(std::function<void()> f) {
	_f = f;
}
FRunnableThread* LambdaRunnable::RunThreaded(FString threadName, std::function<void()> f) {
	static int currentThread = 0;
	LambdaRunnable* runnable = new LambdaRunnable(f);
	FString _threadName = threadName + FString::FromInt(currentThread++);
	runnable->thread = FRunnableThread::Create(runnable, *_threadName);
	return  runnable->thread;
}

uint32 LambdaRunnable::Run() {
	_f();
	return 0;
}

void LambdaRunnable::Exit() {
	delete this;
}