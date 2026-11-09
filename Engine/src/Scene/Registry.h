#pragma once
// Minimal sparse-set ECS: components live in dense arrays per type, entity ids are small integers (0 = null).
#include "Core/Base.h"
#include <unordered_map>
#include <typeindex>
#include <algorithm>
#include <unordered_set>

namespace Stardust {

using EntityID = u32;
static constexpr EntityID kNullEntity = 0;

class Registry;

class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual bool Has(EntityID e) const = 0;
    virtual void Remove(EntityID e) = 0;
    virtual Scope<IComponentPool> Clone() const = 0;
};

template<typename T>
class ComponentPool final : public IComponentPool {
public:
    bool Has(EntityID e) const override { return e < m_Sparse.size() && m_Sparse[e] != kNone; }

    template<typename... Args>
    T& Emplace(EntityID e, Args&&... args) {
        if (Has(e)) return m_Data[m_Sparse[e]] = T{ std::forward<Args>(args)... };
        if (e >= m_Sparse.size()) m_Sparse.resize(e + 1, kNone);
        m_Sparse[e] = (u32)m_Dense.size();
        m_Dense.push_back(e);
        m_Data.push_back(T{ std::forward<Args>(args)... });
        return m_Data.back();
    }

    void Remove(EntityID e) override {
        if (!Has(e)) return;
        u32 idx = m_Sparse[e];
        u32 last = (u32)m_Dense.size() - 1;
        if (idx != last) {
            m_Dense[idx] = m_Dense[last];
            m_Data[idx] = std::move(m_Data[last]);
            m_Sparse[m_Dense[idx]] = idx;
        }
        m_Dense.pop_back();
        m_Data.pop_back();
        m_Sparse[e] = kNone;
    }

    T& Get(EntityID e) { return m_Data[m_Sparse[e]]; }
    T* TryGet(EntityID e) { return Has(e) ? &m_Data[m_Sparse[e]] : nullptr; }
    const std::vector<EntityID>& Entities() const { return m_Dense; }
    size_t Size() const { return m_Dense.size(); }

    Scope<IComponentPool> Clone() const override { return MakeScope<ComponentPool<T>>(*this); }

private:
    static constexpr u32 kNone = ~0u;
    std::vector<u32> m_Sparse;
    std::vector<EntityID> m_Dense;
    std::vector<T> m_Data;
};

class Registry {
public:
    Registry() = default;
    Registry(const Registry& o) { *this = o; }
    Registry& operator=(const Registry& o) {
        if (this == &o) return *this;
        m_Alive = o.m_Alive; m_Free = o.m_Free; m_Disabled=o.m_Disabled;m_Pools.clear();
        for (auto& [t, p] : o.m_Pools) m_Pools.emplace(t, p->Clone());
        return *this;
    }

    EntityID Create() {
        EntityID e;
        if (!m_Free.empty()) { e = m_Free.back(); m_Free.pop_back(); }
        else { e = (EntityID)m_Alive.size(); if (e == kNullEntity) { m_Alive.push_back(false); e = 1; } m_Alive.push_back(false); }
        m_Alive[e] = true;
        return e;
    }

    void Destroy(EntityID e) {
        if (!Valid(e)) return;
        for (auto& [t, p] : m_Pools) p->Remove(e);
        for(auto& [type,entities]:m_Disabled)entities.erase(e);
        m_Alive[e] = false;
        m_Free.push_back(e);
    }

    bool Valid(EntityID e) const { return e != kNullEntity && e < m_Alive.size() && m_Alive[e]; }

    template<typename T, typename... Args> T& Add(EntityID e, Args&&... args) { m_Disabled[std::type_index(typeid(T))].erase(e);return Pool<T>().Emplace(e, std::forward<Args>(args)...); }
    template<typename T> void Remove(EntityID e) { m_Disabled[std::type_index(typeid(T))].erase(e);Pool<T>().Remove(e); }
    template<typename T> bool Has(EntityID e) const { auto* p = FindPool<T>(); return p && p->Has(e); }
    template<typename T> T& Get(EntityID e) { return Pool<T>().Get(e); }
    template<typename T> T* TryGet(EntityID e) { auto* p = FindPool<T>(); return p ? p->TryGet(e) : nullptr; }

    template<typename T> bool IsEnabled(EntityID e) const {if(!Has<T>(e))return false;auto it=m_Disabled.find(std::type_index(typeid(T)));return it==m_Disabled.end()||!it->second.contains(e);}
    template<typename T> void SetEnabled(EntityID e,bool enabled){if(!Has<T>(e))return;auto& disabled=m_Disabled[std::type_index(typeid(T))];if(enabled)disabled.erase(e);else disabled.insert(e);}
    template<typename T> T* TryGetEnabled(EntityID e){return IsEnabled<T>(e)?TryGet<T>(e):nullptr;}

    // Iterate entities that have all listed components: fn(EntityID, A&, B&...)
    template<typename First, typename... Rest, typename Fn>
    void Each(Fn&& fn) {
        auto& first = Pool<First>();
        std::vector<EntityID> ids = first.Entities(); // copy: fn may add/remove components
        for (EntityID e : ids) {
            if (!first.Has(e)||!IsEnabled<First>(e)) continue;
            if constexpr (sizeof...(Rest) > 0) { if (!(IsEnabled<Rest>(e) && ...)) continue; }
            fn(e, first.Get(e), Pool<Rest>().Get(e)...);
        }
    }

    std::vector<EntityID> AllEntities() const {
        std::vector<EntityID> out;
        for (EntityID e = 1; e < m_Alive.size(); ++e) if (m_Alive[e]) out.push_back(e);
        return out;
    }

    void Clear() { m_Alive.clear(); m_Free.clear();m_Disabled.clear(); m_Pools.clear(); }

private:
    template<typename T> ComponentPool<T>& Pool() {
        auto& p = m_Pools[std::type_index(typeid(T))];
        if (!p) p = MakeScope<ComponentPool<T>>();
        return static_cast<ComponentPool<T>&>(*p);
    }
    template<typename T> const ComponentPool<T>* FindPool() const {
        auto it = m_Pools.find(std::type_index(typeid(T)));
        return it == m_Pools.end() ? nullptr : static_cast<const ComponentPool<T>*>(it->second.get());
    }
    template<typename T> ComponentPool<T>* FindPool() {
        auto it = m_Pools.find(std::type_index(typeid(T)));
        return it == m_Pools.end() ? nullptr : static_cast<ComponentPool<T>*>(it->second.get());
    }

    std::vector<bool> m_Alive;
    std::vector<EntityID> m_Free;
    std::unordered_map<std::type_index, Scope<IComponentPool>> m_Pools;
    std::unordered_map<std::type_index,std::unordered_set<EntityID>> m_Disabled;
};

} // namespace Stardust
