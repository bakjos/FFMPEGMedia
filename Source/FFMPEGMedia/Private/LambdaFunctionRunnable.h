#pragma once

#include <HAL/RunnableThread.h>
#include <functional>
#include <HAL/Runnable.h>

class LambdaFunctionRunnable : public FRunnable {
public:
	static FRunnableThread* RunThreaded(FString threadName, std::function<void()> f);
	void Exit() override;
	uint32	Run()	override;
protected:
	LambdaFunctionRunnable(std::function<void()> f);
	std::function<void()> _f;
	FRunnableThread* thread;
};