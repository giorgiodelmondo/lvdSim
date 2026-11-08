// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/fst_loader.hpp>

#include <cstring>
#include <vector>

extern "C" {
#include <fstapi.h>
}

namespace lvd::wave {

namespace {

struct Context {
    WaveStore* store = nullptr;
    std::vector<SignalId> byHandle;  // fst handle -> primary signal (kNoSignal if unknown)
};

SignalKind kindForVarType(unsigned char vt) {
    switch (vt) {
        case FST_VT_VCD_REAL:
        case FST_VT_VCD_REAL_PARAMETER:
        case FST_VT_SV_SHORTREAL:
            return SignalKind::Real;
        case FST_VT_GEN_STRING:
            return SignalKind::String;
        default:
            return SignalKind::Logic;
    }
}

void onValue(void* user, uint64_t time, fstHandle handle, const unsigned char* value) {
    auto* c = static_cast<Context*>(user);
    if (handle >= c->byHandle.size()) return;
    const SignalId id = c->byHandle[handle];
    if (id == kNoSignal) return;

    Signal& s = c->store->mutableSignal(id);
    c->store->extendTimeRange(time);
    switch (s.kind()) {
        case SignalKind::Logic:
            s.appendLogic(time, std::string_view(reinterpret_cast<const char*>(value)));
            break;
        case SignalKind::Real: {
            double d;
            std::memcpy(&d, value, sizeof d);  // native doubles mode
            s.appendReal(time, d);
            break;
        }
        case SignalKind::String:
            s.appendText(time, std::string_view(reinterpret_cast<const char*>(value)));
            break;
    }
}

void onValueVarLen(void* user, uint64_t time, fstHandle handle, const unsigned char* value, uint32_t len) {
    auto* c = static_cast<Context*>(user);
    if (handle >= c->byHandle.size()) return;
    const SignalId id = c->byHandle[handle];
    if (id == kNoSignal) return;

    Signal& s = c->store->mutableSignal(id);
    c->store->extendTimeRange(time);
    const std::string_view v(reinterpret_cast<const char*>(value), len);
    if (s.kind() == SignalKind::String) s.appendText(time, v);
    else if (s.kind() == SignalKind::Logic) s.appendLogic(time, v);
}

}  // namespace

Result<WaveStore> loadFst(const std::string& path) {
    fstReaderContext* r = fstReaderOpen(path.c_str());
    if (r == nullptr) return Result<WaveStore>::failure("cannot open FST file (missing, unreadable or not finalised): " + path);

    WaveStore store;
    store.setTimescaleExponent(fstReaderGetTimescale(r));

    Context ctx;
    ctx.store = &store;
    ctx.byHandle.assign(static_cast<std::size_t>(fstReaderGetMaxHandle(r)) + 1, kNoSignal);

    ScopeId current = kRootScope;
    while (const fstHier* h = fstReaderIterateHier(r)) {
        switch (h->htyp) {
            case FST_HT_SCOPE:
                current = store.addScope(current, std::string(h->u.scope.name, h->u.scope.name_length), h->u.scope.typ);
                break;
            case FST_HT_UPSCOPE:
                if (current != kRootScope) current = store.scope(current).parent;
                break;
            case FST_HT_VAR: {
                const auto& v = h->u.var;
                std::string name(v.name, v.name_length);
                if (v.handle >= ctx.byHandle.size()) break;
                const SignalId existing = ctx.byHandle[v.handle];
                if (existing != kNoSignal) {
                    store.addAlias(current, std::move(name), existing);
                } else {
                    const SignalKind k = kindForVarType(v.typ);
                    ctx.byHandle[v.handle] = store.addSignal(current, std::move(name), k, v.length);
                }
                break;
            }
            default:
                break;  // attributes (array groupings, source stems) are not needed for display
        }
    }

    fstReaderIterBlocksSetNativeDoublesOnCallback(r, 1);
    fstReaderSetFacProcessMaskAll(r);
    fstReaderIterBlocks2(r, onValue, onValueVarLen, &ctx, nullptr);

    store.extendTimeRange(fstReaderGetStartTime(r));
    store.extendTimeRange(fstReaderGetEndTime(r));
    fstReaderClose(r);
    return store;
}

}  // namespace lvd::wave
