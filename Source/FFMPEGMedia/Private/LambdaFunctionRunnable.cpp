#include "LambdaFunctionRunnable.h"


LambdaFunctionRunnable::LambdaFunctionRunnable(std::function<void()> f) {
	_f = f;
}
FRunnableThread* LambdaFunctionRunnable::RunThreaded(FString threadName, std::function<void()> f) {
	static int currentThread = 0;
	LambdaFunctionRunnable* runnable = new LambdaFunctionRunnable(f);
	FString _threadName = threadName + FString::FromInt(currentThread++);
	runnable->thread = FRunnableThread::Create(runnable, *_threadName);
	return  runnable->thread;
}

uint32 LambdaFunctionRunnable::Run() {
	_f();
	return 0;
}

void LambdaFunctionRunnable::Exit() {
	delete this;
}