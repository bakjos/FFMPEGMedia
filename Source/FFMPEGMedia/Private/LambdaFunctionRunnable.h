#pragma once

#include <HAL/Runnable.h>
#include <functional>
#include <HAL/RunnableThread.h>


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