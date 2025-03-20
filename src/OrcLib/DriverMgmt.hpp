#pragma once

#include "DriverMgmt.h"


#pragma managed(push, off)

namespace Orc {

template <typename _Tout, typename _Tin>
inline Result<size_t> Driver::DeviceIoControl(DWORD dwIoControlCode, const Span<_Tin>& input, const Span<_Tout>& output)
{
    using namespace msl::utilities;

    DWORD dwBytesReturned = 0LU;
    if (auto hr = DeviceIoControl(
            dwIoControlCode,
            static_cast<LPVOID>(input.data()),
            SafeInt<DWORD>(input.size() * sizeof(_Tin)),
            static_cast<LPVOID>(output.data()),
            SafeInt<DWORD>(output.size() * sizeof(_Tout)),
            &dwBytesReturned,
            NULL);
        FAILED(hr))
        return SystemError(hr);

    if (dwBytesReturned % sizeof(_Tout))
    {
        Log::Error(
            L"{}::DeviceIoControl({}) returned incomplete/invalid data ({} bytes returned when exepected element "
            L"size is {}",
            m_strServiceName,
            dwIoControlCode,
            dwBytesReturned,
            sizeof(_Tout));
        return Win32Error(ERROR_INVALID_DATA);
    }

    return dwBytesReturned / sizeof(_Tout);
}

template <typename _Tout, typename _Tin>
inline Result<Buffer<_Tout>>
Driver::DeviceIoControl(DWORD dwIoControlCode, const Span<_Tin>& input, DWORD dwExpectedOutputElements)
{
    using namespace msl::utilities;

    Buffer<_Tout> output;
    output.resize(dwExpectedOutputElements);
    DWORD dwBytesReturned = SafeInt<DWORD>(output.size() * sizeof(_Tout));

    if (input.empty())
    {
        if (auto hr = DeviceIoControl(
                dwIoControlCode,
                NULL,
                0LU,
                static_cast<LPVOID>(output.data()),
                SafeInt<DWORD>(output.size() * sizeof(_Tout)),
                &dwBytesReturned,
                NULL);
            FAILED(hr))
            return SystemError(hr);
    }
    else
    {
        if (auto hr = DeviceIoControl(
                dwIoControlCode,
                static_cast<LPVOID>(input.data()),
                SafeInt<DWORD>(input.size() * sizeof(_Tin)),
                static_cast<LPVOID>(output.data()),
                SafeInt<DWORD>(output.size() * sizeof(_Tout)),
                &dwBytesReturned,
                NULL);
            FAILED(hr))
            return SystemError(hr);
    }

    if (dwBytesReturned % sizeof(_Tout))
    {
        Log::Error(
            L"{}::DeviceIoControl({}) returned incomplete/invalid data ({} bytes returned when exepected element "
            L"size is {}",
            m_strServiceName,
            dwIoControlCode,
            dwBytesReturned,
            sizeof(_Tout));
        return SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    }
    output.resize(dwBytesReturned / sizeof(_Tout));
    return output;
}

template <typename _Tin>
inline Result<void> Driver::DeviceIoControl(DWORD dwIoControlCode, const Span<_Tin>& input)
{
    using namespace msl::utilities;

    DWORD dwBytesReturned = 0LU;
    if (auto hr = DeviceIoControl(
            dwIoControlCode,
            static_cast<LPVOID>(input.data()),
            SafeInt<DWORD>(input.size() * sizeof(_Tin)),
            NULL,
            0LU,
            &dwBytesReturned,
            NULL);
        FAILED(hr))
        return SystemError(hr);
    return {};
}

template <typename _Tout>
inline Result<Buffer<_Tout>> Driver::DeviceIoControl(DWORD dwIoControlCode, DWORD dwExpectedOutputElements)
{
    using namespace msl::utilities;

    Buffer<_Tout> output;
    output.resize(dwExpectedOutputElements);

    DWORD dwBytesReturned = 0LU;
    if (auto hr = DeviceIoControl(
            dwIoControlCode,
            NULL,
            0LU,
            static_cast<LPVOID>(output.data()),
            SafeInt<DWORD>(output.capacity() * sizeof(_Tout)),
            &dwBytesReturned,
            NULL);
        FAILED(hr))
        return SystemError(hr);

    if (dwBytesReturned % sizeof(_Tout))
    {
        Log::Error(
            L"{}::DeviceIoControl({}) returned incomplete/invalid data ({} bytes returned when exepected element "
            L"size is {}",
            m_strServiceName,
            dwIoControlCode,
            dwBytesReturned,
            sizeof(_Tout));
        return SystemError(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    }

    output.resize(dwBytesReturned / sizeof(_Tout));
    return output;
}

}

#pragma managed(pop)
