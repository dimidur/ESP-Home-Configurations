#include "meter_stream.h"

#include <cstring>

#include "obis_line.h"

namespace esphome {
namespace electricity_meter_reader {

namespace {

// Text from a 7E1 meter read at 8N1 carries the parity bit on top.
constexpr uint8_t SEVEN_BIT_MASK = 0x7f;

}  // namespace

const char *dropped_telegram_reason(MeterStreamEvent event) {
  switch (event) {
    case MeterStreamEvent::TELEGRAM_RESTART:
      return "cut short by a new start";
    case MeterStreamEvent::TELEGRAM_CRC:
      return "CRC mismatch";
    case MeterStreamEvent::TELEGRAM_DECODE:
      return "not decodable";
    case MeterStreamEvent::TELEGRAM_OVERFLOW:
      return "too long";
    case MeterStreamEvent::NONE:
    case MeterStreamEvent::TELEGRAM:
      break;
  }
  return nullptr;
}

MeterStreamResult MeterStreamReader::feed(uint8_t byte, std::vector<std::string> &lines) {
  if (this->in_telegram_) return this->feed_telegram_(byte, lines);
  memmove(this->recent_, this->recent_ + 1, SML_START_LEN - 1);
  this->recent_[SML_START_LEN - 1] = byte;
  if (sml_is_start(this->recent_)) {
    this->start_telegram_();
  } else if (!this->discarding_) {
    this->feed_text_((char) (byte & SEVEN_BIT_MASK), lines);
  }
  return {};
}

void MeterStreamReader::feed_text_(char received, std::vector<std::string> &lines) {
  if (received == '\n' || this->line_.size() > MAX_LINE_LEN) {
    if (!this->line_.empty()) {
      lines.push_back(this->line_);
      this->line_.clear();
    }
    return;
  }
  if (received != '\r') this->line_ += received;
}

void MeterStreamReader::start_telegram_() {
  // The first start bytes went to the text line; it is not a line.
  this->line_.clear();
  this->telegram_.clear();
  this->telegram_.reserve(MAX_TELEGRAM_LEN);
  this->telegram_.insert(this->telegram_.end(), this->recent_, this->recent_ + SML_START_LEN);
  memset(this->recent_, 0, sizeof(this->recent_));
  this->in_telegram_ = true;
  this->discarding_ = false;
}

MeterStreamResult MeterStreamReader::feed_telegram_(uint8_t byte, std::vector<std::string> &lines) {
  this->telegram_.push_back(byte);
  switch (sml_classify_telegram(this->telegram_, MAX_TELEGRAM_LEN)) {
    case SmlTelegramState::RESTART: {
      const size_t cut_length = this->telegram_.size() - SML_START_LEN;
      this->telegram_.erase(this->telegram_.begin(), this->telegram_.end() - SML_START_LEN);
      return this->dropped_(MeterStreamEvent::TELEGRAM_RESTART, cut_length);
    }
    case SmlTelegramState::COMPLETE:
      return this->finish_telegram_(lines);
    case SmlTelegramState::OVERFLOW: {
      const size_t overflow_length = this->telegram_.size();
      this->in_telegram_ = false;
      this->telegram_.clear();
      return this->dropped_(MeterStreamEvent::TELEGRAM_OVERFLOW, overflow_length);
    }
    case SmlTelegramState::COLLECTING:
      break;
  }
  return {};
}

MeterStreamResult MeterStreamReader::finish_telegram_(std::vector<std::string> &lines) {
  this->in_telegram_ = false;
  const size_t length = this->telegram_.size();
  if (!sml_crc_ok(this->telegram_)) {
    this->telegram_.clear();
    return this->dropped_(MeterStreamEvent::TELEGRAM_CRC, length);
  }
  this->entries_.clear();
  const bool decoded = sml_decode_entries(this->telegram_, this->entries_);
  this->telegram_.clear();
  if (!decoded) return this->dropped_(MeterStreamEvent::TELEGRAM_DECODE, length);

  size_t rendered = 0;
  std::string line;
  for (const auto &entry : this->entries_) {
    if (!obis_line(entry, line)) continue;
    lines.push_back(line);
    rendered++;
  }
  if (rendered > 0) this->lines_per_telegram_ = rendered;
  MeterStreamResult result;
  result.event = MeterStreamEvent::TELEGRAM;
  result.telegram_length = length;
  return result;
}

MeterStreamResult MeterStreamReader::dropped_(MeterStreamEvent event, size_t telegram_length) {
  // Bytes after a dropped telegram belong to a binary stream that is out
  // of step, not to text; skip them until the next start sequence.
  if (!this->in_telegram_) this->discarding_ = true;
  MeterStreamResult result;
  result.event = event;
  result.telegram_length = telegram_length;
  result.lines_lost = this->lines_per_telegram_;
  return result;
}

}  // namespace electricity_meter_reader
}  // namespace esphome
