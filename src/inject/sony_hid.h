// Read-only native HID input. No feature/output reports, mode changes or worker thread.
#pragma once
#include <hidsdi.h>
#include <setupapi.h>
#include <vector>
#include "../consoleport/input/dualsense.h"
#include "../consoleport/input/pad.h"
#pragma comment(lib,"hid.lib")
#pragma comment(lib,"setupapi.lib")

class SonyHid {
    HANDLE handle_=INVALID_HANDLE_VALUE;
    OVERLAPPED ov_={};
    bool pending_=false, owned_=false, connected_=false;
    DWORD retry_=0, lastPacket_=0;
    unsigned reportSize_=0, lastReport_=0;
    unsigned char data_[128]={};
    cp::input::SonyState state_;
    LONG low_[6]={}, high_[6]={65535,65535,65535,65535,65535,65535};
    uint32_t sent_[21]={};
    bool valid_[21]={};

    static HANDLE openPath(const wchar_t* path, unsigned& size) {
        HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                             nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if (h==INVALID_HANDLE_VALUE) return h;
        HIDD_ATTRIBUTES a={}; a.Size=sizeof a;
        PHIDP_PREPARSED_DATA pp=nullptr; HIDP_CAPS caps={};
        bool ok=HidD_GetAttributes(h,&a) && a.VendorID==0x054c &&
                (a.ProductID==0x0ce6 || a.ProductID==0x0df2);
        if (ok && HidD_GetPreparsedData(h,&pp)) {
            ok=HidP_GetCaps(pp,&caps)==HIDP_STATUS_SUCCESS && caps.UsagePage==1 &&
               (caps.Usage==4 || caps.Usage==5) &&
               (caps.InputReportByteLength==64 || caps.InputReportByteLength==78);
            HidD_FreePreparsedData(pp);
        } else ok=false;
        if (!ok) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
        size=caps.InputReportByteLength;
        return h;
    }
    void close() {
        if (handle_!=INVALID_HANDLE_VALUE) {
            // Reap the cancelled operation before reusing its buffer/OVERLAPPED.
            if (pending_) { CancelIo(handle_); DWORD n; GetOverlappedResult(handle_,&ov_,&n,TRUE); }
            CloseHandle(handle_); handle_=INVALID_HANDLE_VALUE;
        }
        if (ov_.hEvent) CloseHandle(ov_.hEvent);
        ov_={}; pending_=false; connected_=false;
        state_=cp::input::SonyState();
    }
public:
    unsigned reportId() const { return lastReport_; }
    bool owned() const { return owned_; }
    void range(int i,LONG lo,LONG hi) { if(i>=0 && i<6) {low_[i]=lo; high_[i]=hi;} }
    // A fallback enumeration is allowed only when the client's own VID/PID is Sony,
    // and there is exactly one readable Sony gamepad interface.
    void open(const wchar_t* exactPath, bool sonyIdentity) {
        if (handle_!=INVALID_HANDLE_VALUE || GetTickCount()-retry_<2000) return;
        retry_=GetTickCount();
        if (exactPath && *exactPath) handle_=openPath(exactPath,reportSize_);
        if (handle_==INVALID_HANDLE_VALUE && sonyIdentity) {
            GUID guid; HidD_GetHidGuid(&guid);
            HDEVINFO list=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
            if (list!=INVALID_HANDLE_VALUE) {
                unsigned matches=0;
                for (DWORD i=0;;++i) {
                    SP_DEVICE_INTERFACE_DATA iface={}; iface.cbSize=sizeof iface;
                    if (!SetupDiEnumDeviceInterfaces(list,nullptr,&guid,i,&iface)) break;
                    DWORD bytes=0;
                    SetupDiGetDeviceInterfaceDetailW(list,&iface,nullptr,0,&bytes,nullptr);
                    if (bytes<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
                    std::vector<unsigned char> storage(bytes);
                    auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
                    detail->cbSize=sizeof *detail;
                    if (!SetupDiGetDeviceInterfaceDetailW(list,&iface,detail,bytes,nullptr,nullptr)) continue;
                    unsigned size=0; HANDLE h=openPath(detail->DevicePath,size);
                    if (h==INVALID_HANDLE_VALUE) continue;
                    ++matches;
                    if (matches==1) {handle_=h;reportSize_=size;} else CloseHandle(h);
                }
                SetupDiDestroyDeviceInfoList(list);
                if (matches>1) {CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;}
            }
        }
        if (handle_!=INVALID_HANDLE_VALUE) {
            ov_.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            if (!ov_.hEvent) close();
        }
    }
    // Returns true exactly once when validated packets take over from DirectInput.
    bool poll() {
        const bool wasOwned=owned_;
        if (handle_==INVALID_HANDLE_VALUE) return false;
        for (int budget=0;budget<64;++budget) {
            DWORD n=0;
            if (pending_) {
                if (!GetOverlappedResult(handle_,&ov_,&n,FALSE)) {
                    if (GetLastError()!=ERROR_IO_INCOMPLETE) close();
                    break;
                }
                pending_=false;
            } else {
                ResetEvent(ov_.hEvent);
                if (!ReadFile(handle_,data_,reportSize_,&n,&ov_)) {
                    if (GetLastError()==ERROR_IO_PENDING) pending_=true; else close();
                    break;
                }
            }
            if (cp::input::decodeDualSense(data_,n,state_)) {
                lastReport_=data_[0]; owned_=connected_=true; lastPacket_=GetTickCount();
            }
        }
        if (connected_ && GetTickCount()-lastPacket_>1000) {
            state_=cp::input::SonyState(); connected_=false;
        }
        return owned_ && !wasOwned;
    }
    // Retain unsent state across small client buffers. Reserve half for synthesized buttons.
    void events(cp::input::DiEvent* out,uint32_t& count,uint32_t capacity,bool focused=true) {
        const cp::input::SonyState neutral;
        const cp::input::SonyState& current=focused ? state_ : neutral;
        count=0;
        uint32_t limit=capacity/2; if(!limit && capacity) limit=1;
        for(int i=0;i<21 && count<limit;++i) {
            uint32_t value,ofs;
            if(i<6) {
                int raw=current.axes[i];
                // Exact centre, with a small radial-independent deadzone for game turn.
                const bool stick=i==0 || i==1 || i==2 || i==5;
                if(stick && raw>=116 && raw<=139) value=uint32_t((int64_t(low_[i])+high_[i])/2);
                else value=uint32_t(low_[i]+int64_t(high_[i]-low_[i])*raw/255);
                ofs=uint32_t(i*4);
            } else if(i==6) {value=current.pov;ofs=32;}
            else {value=(current.buttons&(1u<<(i-7))) ? 0x80u : 0;ofs=48+i-7;}
            if(!valid_[i] || sent_[i]!=value) {
                out[count++]={ofs,value,GetTickCount(),0,0};
                valid_[i]=true;sent_[i]=value;
            }
        }
    }
};
