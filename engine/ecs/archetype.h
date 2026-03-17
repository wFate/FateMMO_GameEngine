#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <type_traits>

#include "engine/ecs/component_registry.h"
#include "engine/ecs/entity_handle.h"
#include "engine/memory/arena.h"

namespace fate {

using ArchetypeId = uint32_t;
using RowIndex    = uint32_t;

static constexpr uint32_t ARCHETYPE_INITIAL_CAPACITY = 64;

// ==========================================================================
// ArchetypeColumn — type-erased column storing N instances of one component
// ==========================================================================
struct ArchetypeColumn {
    CompId   typeId   = 0;
    size_t   elemSize = 0;
    uint8_t* data     = nullptr;
    using DestroyFn = void(*)(void*, size_t);
    DestroyFn destroyFn = nullptr;  // null for trivially-destructible types

    void* at(uint32_t row) const {
        return data + static_cast<size_t>(row) * elemSize;
    }

    void swapRows(uint32_t a, uint32_t b) {
        if (a == b) return;
        uint8_t* pa = data + static_cast<size_t>(a) * elemSize;
        uint8_t* pb = data + static_cast<size_t>(b) * elemSize;
        // In-place swap via stack buffer (elemSize is typically small)
        for (size_t i = 0; i < elemSize; ++i) {
            uint8_t tmp = pa[i];
            pa[i] = pb[i];
            pb[i] = tmp;
        }
    }

    void copyRow(uint32_t dst, uint32_t src) {
        if (dst == src) return;
        std::memcpy(data + static_cast<size_t>(dst) * elemSize,
                     data + static_cast<size_t>(src) * elemSize,
                     elemSize);
    }

    void zeroRow(uint32_t row) {
        std::memset(data + static_cast<size_t>(row) * elemSize, 0, elemSize);
    }

    void destroyRange(uint32_t start, uint32_t count) {
        if (destroyFn) {
            destroyFn(data + static_cast<size_t>(start) * elemSize, count);
        }
    }
};

// ==========================================================================
// Archetype — a unique set of component types with SoA column storage
// ==========================================================================
struct Archetype {
    ArchetypeId                           id       = 0;
    std::vector<CompId>                   typeIds;       // sorted
    std::vector<ArchetypeColumn>          columns;
    std::vector<EntityHandle>             handles;
    uint32_t                              count    = 0;
    uint32_t                              capacity = 0;
    std::unordered_map<CompId, size_t>    typeToColumn;

    bool hasType(CompId cid) const {
        return typeToColumn.find(cid) != typeToColumn.end();
    }

    size_t columnIndex(CompId cid) const {
        auto it = typeToColumn.find(cid);
        return (it != typeToColumn.end()) ? it->second : SIZE_MAX;
    }
};

// ==========================================================================
// TypeInfo — runtime metadata for a registered component type
// ==========================================================================
struct TypeInfo {
    CompId   id        = 0;
    size_t   size      = 0;
    size_t   alignment = 0;
    ArchetypeColumn::DestroyFn destroyFn = nullptr;
};

// ==========================================================================
// ArchetypeStorage — manages all archetypes for a World, backed by Arena&
// ==========================================================================
class ArchetypeStorage {
public:
    explicit ArchetypeStorage(Arena& arena) : arena_(arena) {}

    // -- Type registration --------------------------------------------------

    template<typename T>
    void registerType() {
        CompId cid = componentId<T>();
        if (typeInfos_.find(cid) != typeInfos_.end()) return;

        TypeInfo info;
        info.id        = cid;
        info.size      = sizeof(T);
        info.alignment = alignof(T);

        if constexpr (!std::is_trivially_destructible_v<T>) {
            info.destroyFn = [](void* ptr, size_t count) {
                T* arr = static_cast<T*>(ptr);
                for (size_t i = 0; i < count; ++i) arr[i].~T();
            };
        }

        typeInfos_[cid] = info;
    }

    // -- Archetype lookup / creation ----------------------------------------

    ArchetypeId findOrCreateArchetype(std::vector<CompId> typeIds) {
        std::sort(typeIds.begin(), typeIds.end());

        uint64_t hash = hashSignature(typeIds);
        auto it = signatureToArchetype_.find(hash);
        if (it != signatureToArchetype_.end()) {
            // Verify it's not a hash collision
            if (archetypes_[it->second].typeIds == typeIds) {
                return it->second;
            }
        }

        // Create new archetype
        ArchetypeId aid = static_cast<ArchetypeId>(archetypes_.size());
        archetypes_.emplace_back();
        Archetype& arch = archetypes_.back();
        arch.id       = aid;
        arch.typeIds  = typeIds;
        arch.count    = 0;
        arch.capacity = ARCHETYPE_INITIAL_CAPACITY;

        // Create columns
        arch.columns.resize(typeIds.size());
        for (size_t i = 0; i < typeIds.size(); ++i) {
            CompId cid = typeIds[i];
            auto infoIt = typeInfos_.find(cid);
            // Type must be registered
            if (infoIt == typeInfos_.end()) continue;

            const TypeInfo& ti = infoIt->second;
            arch.columns[i].typeId    = cid;
            arch.columns[i].elemSize  = ti.size;
            arch.columns[i].destroyFn = ti.destroyFn;
            arch.columns[i].data      = allocateColumn(ti.size, ti.alignment, arch.capacity);
            arch.typeToColumn[cid]    = i;
        }

        // Allocate handle storage
        arch.handles.resize(arch.capacity);

        signatureToArchetype_[hash] = aid;
        ++version_;
        return aid;
    }

    // -- Entity manipulation ------------------------------------------------

    RowIndex addEntity(ArchetypeId archId, EntityHandle handle) {
        Archetype& arch = archetypes_[archId];

        if (arch.count >= arch.capacity) {
            growArchetype(arch);
        }

        RowIndex row = arch.count;
        arch.handles[row] = handle;

        // Zero-init all component columns for this row
        for (auto& col : arch.columns) {
            col.zeroRow(row);
        }

        ++arch.count;
        return row;
    }

    // Returns the handle that was swapped into the vacated row (or NULL_ENTITY_HANDLE if last)
    EntityHandle removeEntity(ArchetypeId archId, RowIndex row) {
        Archetype& arch = archetypes_[archId];
        uint32_t lastRow = arch.count - 1;

        EntityHandle swapped = NULL_ENTITY_HANDLE;

        if (row != lastRow) {
            // Swap-and-pop: move last row into removed row
            for (auto& col : arch.columns) {
                col.copyRow(row, lastRow);
            }
            arch.handles[row] = arch.handles[lastRow];
            swapped = arch.handles[row];
        }

        --arch.count;
        return swapped;
    }

    // Migrate entity from one archetype to another (add or remove a component type)
    // Returns the new archetype ID
    ArchetypeId migrateEntity(ArchetypeId fromArchId, RowIndex fromRow,
                              EntityHandle handle, CompId typeId, bool adding) {
        Archetype& fromArch = archetypes_[fromArchId];

        // Build new type set
        std::vector<CompId> newTypes = fromArch.typeIds;
        if (adding) {
            if (std::find(newTypes.begin(), newTypes.end(), typeId) == newTypes.end()) {
                newTypes.push_back(typeId);
            }
        } else {
            newTypes.erase(std::remove(newTypes.begin(), newTypes.end(), typeId),
                           newTypes.end());
        }

        ArchetypeId toArchId = findOrCreateArchetype(newTypes);
        // Re-fetch fromArch in case findOrCreateArchetype reallocated archetypes_
        Archetype& fromArchRef = archetypes_[fromArchId];
        Archetype& toArch      = archetypes_[toArchId];

        // Add entity to destination archetype
        RowIndex toRow = addEntity(toArchId, handle);
        // Re-fetch again after addEntity (which may grow and reallocate)
        Archetype& fromArchFinal = archetypes_[fromArchId];
        Archetype& toArchFinal   = archetypes_[toArchId];

        // Copy shared component data
        for (size_t i = 0; i < fromArchFinal.typeIds.size(); ++i) {
            CompId cid = fromArchFinal.typeIds[i];
            size_t toColIdx = toArchFinal.columnIndex(cid);
            if (toColIdx != SIZE_MAX) {
                std::memcpy(toArchFinal.columns[toColIdx].at(toRow),
                            fromArchFinal.columns[i].at(fromRow),
                            fromArchFinal.columns[i].elemSize);
            }
        }

        // Remove from source archetype (swap-and-pop)
        removeEntity(fromArchId, fromRow);

        return toArchId;
    }

    // -- Column access ------------------------------------------------------

    template<typename T>
    T* getColumn(ArchetypeId archId) {
        Archetype& arch = archetypes_[archId];
        CompId cid = componentId<T>();
        size_t colIdx = arch.columnIndex(cid);
        if (colIdx == SIZE_MAX) return nullptr;
        return reinterpret_cast<T*>(arch.columns[colIdx].data);
    }

    EntityHandle* getHandles(ArchetypeId archId) {
        return archetypes_[archId].handles.data();
    }

    uint32_t entityCount(ArchetypeId archId) const {
        return archetypes_[archId].count;
    }

    const Archetype& getArchetype(ArchetypeId archId) const {
        return archetypes_[archId];
    }

    size_t archetypeCount() const {
        return archetypes_.size();
    }

    uint64_t version() const { return version_; }

    // -- Iteration ----------------------------------------------------------

    void forEachMatchingArchetype(const std::vector<CompId>& required,
                                  const std::function<void(Archetype&)>& fn) {
        for (auto& arch : archetypes_) {
            if (arch.count == 0) continue;
            bool matches = true;
            for (CompId cid : required) {
                if (!arch.hasType(cid)) {
                    matches = false;
                    break;
                }
            }
            if (matches) fn(arch);
        }
    }

    // -- Cleanup ------------------------------------------------------------

    void destroyAll() {
        for (auto& arch : archetypes_) {
            for (auto& col : arch.columns) {
                col.destroyRange(0, arch.count);
            }
            arch.count = 0;
        }
    }

private:
    Arena& arena_;
    std::vector<Archetype>                         archetypes_;
    std::unordered_map<CompId, TypeInfo>            typeInfos_;
    std::unordered_map<uint64_t, ArchetypeId>      signatureToArchetype_;
    uint64_t                                       version_ = 0;

    // -- Helpers ------------------------------------------------------------

    uint8_t* allocateColumn(size_t elemSize, size_t alignment, uint32_t capacity) {
        size_t totalBytes = elemSize * capacity;
        void* mem = arena_.push(totalBytes, alignment);
        if (mem) std::memset(mem, 0, totalBytes);
        return static_cast<uint8_t*>(mem);
    }

    void growArchetype(Archetype& arch) {
        uint32_t newCap = arch.capacity * 2;

        // Grow each column: allocate new array from arena, copy old data
        for (size_t i = 0; i < arch.columns.size(); ++i) {
            auto& col = arch.columns[i];
            auto infoIt = typeInfos_.find(col.typeId);
            size_t alignment = (infoIt != typeInfos_.end()) ? infoIt->second.alignment : 16;

            uint8_t* newData = allocateColumn(col.elemSize, alignment, newCap);
            std::memcpy(newData, col.data, col.elemSize * static_cast<size_t>(arch.count));
            // Old data is abandoned — reclaimed on arena reset
            col.data = newData;
        }

        // Grow handle array
        std::vector<EntityHandle> newHandles(newCap);
        std::memcpy(newHandles.data(), arch.handles.data(),
                     sizeof(EntityHandle) * arch.count);
        arch.handles = std::move(newHandles);

        arch.capacity = newCap;
    }

    static uint64_t hashSignature(const std::vector<CompId>& typeIds) {
        // Golden ratio hash mixing
        uint64_t hash = 0;
        for (CompId cid : typeIds) {
            hash ^= static_cast<uint64_t>(cid) * 0x9E3779B97F4A7C15ULL;
            hash = (hash << 13) | (hash >> 51);
        }
        return hash;
    }
};

} // namespace fate
