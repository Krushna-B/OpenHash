#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

enum class SlotState : uint8_t { Empty, Occupied, Tombstone };

struct Slot {
  SlotState state = SlotState::Empty;
  std::string key;
  std::string value;
};

class KVStoreOpen {
private:
  std::vector<Slot> slots;
  float _resize_threshold{};
  size_t _size{};
  size_t _used{};

  size_t index_for(const std::string &key) const {
    return std::hash<std::string>{}(key) % slots.size();
  }

  void resize() {
    std::vector<Slot> new_slots(slots.size() * 2);
    for (auto &slot : slots) {
      if (slot.state != SlotState::Occupied) {
        continue;
      }
      size_t i = std::hash<std::string>{}(slot.key) % new_slots.size();
      // Walk the slots
      while (new_slots[i].state == SlotState::Occupied) {
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
  explicit KVStoreOpen(size_t slot_count, float _resize_threshold = 0.75)
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
      Slot &s = slots[i];
      if (s.state == SlotState::Occupied && s.key == key) {
        s.value = value;
        return;
      }
      if (s.state == SlotState::Tombstone && insert_at == slots.size()) {
        insert_at = i;
      }
      if (s.state == SlotState::Empty) {
        break;
      }
      i = (i + 1) % slots.size();
    }

    if (insert_at == slots.size()) {
      insert_at = i;
      _used++;
    }

    slots[insert_at].key = key;
    slots[insert_at].value = value;
    slots[insert_at].state = SlotState::Occupied;
    _size++;
    return;
  }

  bool get(const std::string &key, std::string &out) const {
    size_t i = index_for(key);

    while (true) {
      const Slot &s = slots[i];
      if (s.state == SlotState::Empty) {
        return false;
      }
      if (s.state == SlotState::Occupied && s.key == key) {
        out = s.value;
        return true;
      }
      i = (i + 1) % slots.size();
    }
  }

  bool del(const std::string &key) {
    size_t i = index_for(key);

    while (true) {
      Slot &s = slots[i];
      if (s.state == SlotState::Empty) {
        return false;
      }
      if (s.state == SlotState::Occupied && s.key == key) {
        s.state = SlotState::Tombstone;
        s.key.clear();
        s.value.clear();
        _size--;
        return true;
      }
      i = (i + 1) % slots.size();
    }
  }

  size_t size() const { return _size; };
};
