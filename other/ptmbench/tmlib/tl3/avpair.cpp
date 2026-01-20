#include "avpair.h"
#include <ostream>

AVPair::AVPair() {}
AVPair::AVPair(AVPair* _Next, AVPair* _Prev, long _Ordinal)
    : Next(_Next), Prev(_Prev), addr(0), value(0), LockFor(0), rdv(0), Ordinal(_Ordinal), hashTableEntry(0)
    // : Next(_Next), Prev(_Prev), addr(0), value(0), LockFor(0), rdv(0), Ordinal(_Ordinal), hashTableIx(-1) //hashTableEntry(0)
{
    //value.l = 0;
}

void AVPair::validateInvariants() {

}

std::ostream& operator<<(std::ostream& out, const AVPair& obj) {
    return out<<"[addr="<<renamePointer((void*) obj.addr)
            //<<" val="<<obj.value.l
            <<" lock@"<<renamePointer(obj.LockFor)
            //<<" prev="<<obj.Prev
            //<<" next="<<obj.Next
            //<<" ord="<<obj.Ordinal
            //<<" rdv="<<obj.rdv<<"@"<<(uintptr_t)(long*)&obj
            <<"]@"<<renamePointer(&obj);
}
