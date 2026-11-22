#pragma once
// Ray tracing pipeline creation under SEH. One AMD laptop's driver (RX 6500M, crash report 11-21: an access
// violation in D3D12Core.dll under amdxc64.dll) faulted inside CreateStateObject while the renderer set up
// ray tracing at startup, before the player ever turned it on, and the game closed before its first frame.
// A fault now costs ray tracing for the session, not the game: dxr_driver_faulted() stays true and the
// renderer skips the rest of its ray tracing setup. No C++ objects live in the guarded function, as SEH
// requires. MELEE_TEST_DXR_FAULT=1 (hidden test runs only) faults the first call on purpose.
#include <windows.h>
#include <d3d12.h>
#include <atomic>
#include <cstdlib>

namespace gx {

inline std::atomic<bool>& dxr_driver_fault_flag() { static std::atomic<bool> f{false}; return f; }
inline bool dxr_driver_faulted() { return dxr_driver_fault_flag().load(std::memory_order_relaxed); }
bool dxr_test_fault();   // MELEE_TEST_DXR_FAULT=1 (gx_dxr_path_tracer.cpp, through the test policy)

inline HRESULT dxr_guarded_create_state_object_raw(ID3D12Device5* device, const D3D12_STATE_OBJECT_DESC* desc,
                                                   ID3D12StateObject** out, bool test_fault) {
  __try {
    if (test_fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    return device->CreateStateObject(desc, __uuidof(ID3D12StateObject), reinterpret_cast<void**>(out));
  } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                  ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
    *out = nullptr;
    return E_ABORT;
  }
}

// E_ABORT: the driver faulted (or an earlier call did, so this one was not made).
inline HRESULT dxr_guarded_create_state_object(ID3D12Device5* device, const D3D12_STATE_OBJECT_DESC* desc,
                                               ID3D12StateObject** out) {
  *out = nullptr;
  if (dxr_driver_faulted()) return E_ABORT;
  const HRESULT hr = dxr_guarded_create_state_object_raw(device, desc, out, dxr_test_fault());
  if (hr == E_ABORT) dxr_driver_fault_flag().store(true, std::memory_order_relaxed);
  return hr;
}

}  // namespace gx
