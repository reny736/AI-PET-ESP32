#pragma once

#include <Arduino.h>

namespace doubao {

enum MessageType : uint8_t {
    kClientFullRequest = 0x01,
    kClientAudioOnlyRequest = 0x02,
    kServerFullResponse = 0x09,
    kServerAudioResponse = 0x0B,
    kServerErrorResponse = 0x0F
};

enum MessageFlag : uint8_t {
    kNoSequence = 0x00,
    kPositiveSequence = 0x01,
    kNegativeSequence = 0x02,
    kNegativeSequenceOne = 0x03,
    kWithEvent = 0x04
};

enum Serialization : uint8_t {
    kNoSerialization = 0x00,
    kJsonSerialization = 0x01
};

enum Compression : uint8_t {
    kNoCompression = 0x00,
    kGzipCompression = 0x01
};

enum EventCode : uint32_t {
    kStartConnection = 1,
    kFinishConnection = 2,
    kStartSession = 100,
    kFinishSession = 102,
    kTaskRequest = 200,
    kSayHello = 300,
    kConnectionStarted = 50,
    kConnectionFailed = 51,
    kConnectionFinished = 52,
    kSessionStarted = 150,
    kSessionFinished = 152,
    kSessionFailed = 153,
    kUsageResponse = 154,
    kTtsSentenceStart = 350,
    kTtsSentenceEnd = 351,
    kTtsResponse = 352,
    kTtsEnded = 359,
    kAsrInfo = 450,
    kAsrResponse = 451,
    kAsrEnded = 459,
    kChatResponse = 550,
    kChatEnded = 559,
    kDialogError = 599
};

struct SessionConfig {
    String bot_name;
    String system_role;
    String speaking_style;
    String speaker;
    String location_city;
    String model;
    uint32_t recv_timeout_seconds = 10;
    bool strict_audit = false;
};

struct ServerMessage {
    uint8_t message_type = 0;
    uint8_t flags = 0;
    uint8_t serialization = 0;
    uint8_t compression = 0;

    bool has_sequence = false;
    uint32_t sequence = 0;

    bool has_event = false;
    uint32_t event = 0;

    String session_id;
    String payload_json;

    uint8_t* payload = nullptr;
    size_t payload_length = 0;
    bool payload_owned = false;

    bool is_error = false;
    uint32_t error_code = 0;
};

class Protocol {
public:
    static bool buildStartConnectionFrame(uint8_t*& out, size_t& out_len);
    static bool buildStartSessionFrame(
        const String& session_id,
        const SessionConfig& config,
        uint8_t*& out,
        size_t& out_len);
    static bool buildAudioFrame(
        const String& session_id,
        const uint8_t* audio,
        size_t audio_len,
        uint8_t*& out,
        size_t& out_len);
    static bool buildAudioFrameInto(
        const String& session_id,
        const uint8_t* audio,
        size_t audio_len,
        uint8_t* out,
        size_t out_capacity,
        size_t& out_len);
    static bool buildSayHelloFrame(
        const String& session_id,
        const String& content,
        uint8_t*& out,
        size_t& out_len);

    static bool parseServerMessage(
        const uint8_t* data,
        size_t len,
        ServerMessage& message);
    static void freeServerMessage(ServerMessage& message);

private:
    static bool buildCommonFrame(
        uint8_t message_type,
        uint8_t flags,
        uint8_t serialization,
        uint8_t compression,
        uint32_t event,
        const String& session_id,
        const uint8_t* payload,
        size_t payload_len,
        uint8_t*& out,
        size_t& out_len);
    static void writeUint32BE(uint8_t* target, uint32_t value);
    static uint32_t readUint32BE(const uint8_t* source);
    static String bytesToString(const uint8_t* data, size_t len);
};

}  // namespace doubao
