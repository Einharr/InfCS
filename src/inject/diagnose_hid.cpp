#include <windows.h>
#include <cstdio>
#include "sony_hid.h"
int main() {
    SonyHid hid;
    for (int t=0;t<150;++t) {
        hid.open(L"",true);
        if(hid.poll()) std::printf("PASS: native Sony HID report 0x%02x decoded (read-only)\n",hid.reportId());
        if(hid.owned()) {
            cp::input::DiEvent e[64]; uint32_t n=0; hid.events(e,n,64);
            for(uint32_t i=0;i<n;++i) std::printf("offset=%u value=%u\n",e[i].ofs,e[i].data);
        }
        Sleep(20);
    }
    if(!hid.owned()) std::puts("FAIL: no uniquely identifiable readable Sony HID stream");
    return hid.owned()?0:1;
}
