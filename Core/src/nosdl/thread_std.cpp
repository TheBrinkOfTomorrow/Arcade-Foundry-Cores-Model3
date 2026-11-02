/*
 * Supermodel's OSD threads (OSD/Thread.h) on the C++ standard library, for
 * cores built without SDL (Apple). Same behaviour as OSD/SDL/Thread.cpp.
 */
#include "OSD/Thread.h"
#include "OSD/Logger.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace {

struct ThreadImpl
{
  std::thread thread;
  int status = 0;
  UINT32 id = 0;
};

struct Semaphore
{
  std::mutex mutex;
  std::condition_variable cv;
  UINT32 value;
};

const auto s_start = std::chrono::steady_clock::now();

}  // namespace

void CThread::Sleep(UINT32 ms)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

UINT32 CThread::GetTicks()
{
  return UINT32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_start).count());
}

CThread *CThread::CreateThread(const std::string &name, ThreadStart start, void *startParam)
{
  auto *impl = new ThreadImpl;
  impl->thread = std::thread([impl, start, startParam] { impl->status = start(startParam); });
  impl->id = UINT32(std::hash<std::thread::id>()(impl->thread.get_id()));
  return new CThread(name, impl);
}

CSemaphore *CThread::CreateSemaphore(UINT32 initVal)
{
  auto *impl = new Semaphore;
  impl->value = initVal;
  return new CSemaphore(impl);
}

CCondVar *CThread::CreateCondVar()
{
  return new CCondVar(new std::condition_variable_any);
}

CMutex *CThread::CreateMutex()
{
  return new CMutex(new std::mutex);
}

const char *CThread::GetLastError()
{
  return "";
}

CThread::CThread(const std::string &name, void *impl)
  : m_name(name), m_impl(impl)
{
}

CThread::~CThread()
{
  if (m_impl)
    ErrorLog("Runaway thread error. A thread was not properly halted: %s\n", GetName().c_str());
}

const std::string &CThread::GetName() const
{
  return m_name;
}

UINT32 CThread::GetId()
{
  return m_impl ? static_cast<ThreadImpl *>(m_impl)->id : 0;
}

int CThread::Wait()
{
  if (!m_impl)
    return -1;
  auto *impl = static_cast<ThreadImpl *>(m_impl);
  impl->thread.join();
  int status = impl->status;
  delete impl;
  m_impl = nullptr;
  return status;
}

CSemaphore::CSemaphore(void *impl) : m_impl(impl) {}
CSemaphore::~CSemaphore() { delete static_cast<Semaphore *>(m_impl); }

UINT32 CSemaphore::GetValue()
{
  auto *s = static_cast<Semaphore *>(m_impl);
  std::lock_guard<std::mutex> lock(s->mutex);
  return s->value;
}

bool CSemaphore::Wait()
{
  auto *s = static_cast<Semaphore *>(m_impl);
  std::unique_lock<std::mutex> lock(s->mutex);
  s->cv.wait(lock, [s] { return s->value > 0; });
  s->value--;
  return true;
}

bool CSemaphore::Post()
{
  auto *s = static_cast<Semaphore *>(m_impl);
  {
    std::lock_guard<std::mutex> lock(s->mutex);
    s->value++;
  }
  s->cv.notify_one();
  return true;
}

CCondVar::CCondVar(void *impl) : m_impl(impl) {}
CCondVar::~CCondVar() { delete static_cast<std::condition_variable_any *>(m_impl); }

bool CCondVar::Wait(CMutex *mutex)
{
  // The caller holds the mutex, as with SDL_CondWait.
  static_cast<std::condition_variable_any *>(m_impl)->wait(*static_cast<std::mutex *>(mutex->m_impl));
  return true;
}

bool CCondVar::Signal()
{
  static_cast<std::condition_variable_any *>(m_impl)->notify_one();
  return true;
}

bool CCondVar::SignalAll()
{
  static_cast<std::condition_variable_any *>(m_impl)->notify_all();
  return true;
}

CMutex::CMutex(void *impl) : m_impl(impl) {}
CMutex::~CMutex() { delete static_cast<std::mutex *>(m_impl); }

bool CMutex::Lock()
{
  static_cast<std::mutex *>(m_impl)->lock();
  return true;
}

bool CMutex::Unlock()
{
  static_cast<std::mutex *>(m_impl)->unlock();
  return true;
}
