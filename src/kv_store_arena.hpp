#pragma once
#include "arena.hpp"
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct ArenaRef {
  const char *data = nullptr;
  uint32_t len = 0;
};

// Helper function for Arena

enum class ArenaSlotState : uint8_t { Empty, Occupied, Tombstone };

struct ArenaSlot {
  ArenaSlotState state = ArenaSlotState::Empty;
  ArenaRef key;
  ArenaRef value;
};

class KVStoreArena {
private:
  std::vector<ArenaSlot> slots;
  Arena arena;
  float _resize_threshold{};
  size_t _size{};
  size_t _used{};

  // Helpers
  size_t index_for(const std::string &key) const {
    return std::hash<std::string>{}(key) % slots.size();
  }

  static std::string_view view(const ArenaRef &ref) {
    return std::string_view(ref.data, ref.len);
  }
  ArenaRef copy_in(const std::string &s) {
    char *mem = arena.alloc(s.size());
    std::memcpy(mem, s.data(), s.size());
    return ArenaRef{mem, static_cast<uint32_t>(s.size())};
  }

  void resize() {
    std::vector<ArenaSlot> new_slots(slots.size() * 2);
    for (auto &slot : slots) {
      if (slot.state != ArenaSlotState::Occupied) {
        continue;
      }
      size_t i =
          std::hash<std::string_view>{}(view(slot.key)) % new_slots.size();
      // Walk the slots
      while (new_slots[i].state == ArenaSlotState::Occupied) {
        i = (i + 1) % new_slots.size();
      }
      // Place new slots
      new_slots[i] = std::move(slot);
    }
    slots = std::move(new_slots);
    _used = _size;
  }

public:
  // Constructor
  explicit KVStoreArena(size_t slot_count, float _resize_threshold = 0.75)
      : slots(slot_count), _resize_threshold(_resize_threshold) {}

  void set(const std::string &key, const std::string &value) {
    // Resize if below threshold
    if ((_used + 1.0) / slots.size() > _resize_threshold) {
      resize();
    }

    size_t i = index_for(key);
    size_t insert_at =
        slots.size(); // Index of insertion, inital set to end value

    while (true) {
      ArenaSlot &s = slots[i];
      if (s.state == ArenaSlotState::Occupied && view(s.key) == key) {
        s.value = copy_in(value);
        return;
      }
      if (s.state == ArenaSlotState::Tombstone && insert_at == slots.size()) {
        insert_at = i;
      }
      if (s.state == ArenaSlotState::Empty) {
        break;
      }
      i = (i + 1) % slots.size();
    }

    if (insert_at == slots.size()) {
      insert_at = i;
      _used++;
    }

    slots[insert_at].key = copy_in(key);
    slots[insert_at].value = copy_in(value);
    slots[insert_at].state = ArenaSlotState::Occupied;
    _size++;
    return;
  }

  bool get(const std::string &key, std::string &out) const {
    size_t i = index_for(key);

    while (true) {
      const ArenaSlot &s = slots[i];
      if (s.state == ArenaSlotState::Empty) {
        return false;
      }
      if (s.state == ArenaSlotState::Occupied && view(s.key) == key) {
        out.assign(s.value.data, s.value.len);
        return true;
      }
      i = (i + 1) % slots.size();
    }
  }

  bool del(const std::string &key) {
    size_t i = index_for(key);

    while (true) {
      ArenaSlot &s = slots[i];
      if (s.state == ArenaSlotState::Empty) {
        return false;
      }
      if (s.state == ArenaSlotState::Occupied && view(s.key) == key) {
        s.state = ArenaSlotState::Tombstone;
        s.key = ArenaRef{};
        s.value = ArenaRef{};
        _size--;
        return true;
      }
      i = (i + 1) % slots.size();
    }
  }

  size_t size() const { return _size; };
};
