/**
 * @file memcached_tap_client.hpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MEMCACHED_TAP_CLIENT_H__
#define MEMCACHED_TAP_CLIENT_H__

#include <vector>
#include <string>
#include <cstdint>
#include <arpa/inet.h>
#include <boost/detail/endian.hpp>
#include <log.h>

// Simple Object Definitions
typedef uint16_t             VBucket;
typedef std::vector<VBucket>  VBucketList;
typedef VBucketList::const_iterator VBucketIter;

#define HDR_GET(RAW, FIELD) \
  ::Memcached::Utils::network_to_host(((MsgHdr*)(RAW))->FIELD)

// Namespace for memcached operations
namespace Memcached
{
  namespace Utils
  {
    inline uint8_t host_to_network(uint8_t v) {return v;}
    inline uint8_t network_to_host(uint8_t v) {return v;}
    inline uint16_t host_to_network(uint16_t v) {return htons(v);}
    inline uint16_t network_to_host(uint16_t v) {return ntohs(v);}
    inline uint32_t host_to_network(uint32_t v) {return htonl(v);}
    inline uint32_t network_to_host(uint32_t v) {return ntohl(v);}
    inline uint64_t host_to_network(uint64_t v)
    {
#ifdef BOOST_LITTLE_ENDIAN
      uint64_t low = htonl(static_cast<uint32_t>(v >> 32));
      uint64_t high = htonl(static_cast<uint32_t>(v & 0xFFFFFFFF));
      return (low | (high << 32));
#else
      return v;
#endif
    }
    inline uint64_t network_to_host(uint64_t v) { return host_to_network(v); }

    void write(const std::string& value, std::string& str);
    template<class T> void write(const T& value, std::string& str)
    {
      T network_value = host_to_network(value);
      str.append(reinterpret_cast<const char*>(&network_value), sizeof(T));
    }
  }

  enum struct OpCode
  {
    GET = 0x00,
    SET = 0x01,
    ADD = 0x02,
    REPLACE = 0x03,
    DELETE = 0x04,
    QUIT = 0x07,
    VERSION = 0x0b,
    GETK = 0x0c,
    TAP_CONNECT = 0x40,
    TAP_MUTATE = 0x41,
    SET_VBUCKET = 0x3d
  };

  enum struct ResultCode
  {
    NO_ERROR = 0X0000,
    KEY_NOT_FOUND = 0X0001,
    KEY_EXISTS = 0X0002,
    VALUE_TOO_LARGE = 0X0003,
    INVALID_ARGUMENTS = 0X0004,
    ITEM_NOT_STORED = 0X0005,
    INCR_DECR_ON_NON_NUMERIC_VALUE = 0X0006,
    THE_VBUCKET_BELONGS_TO_ANOTHER_SERVER = 0X0007,
    AUTHENTICATION_ERROR = 0X0008,
    AUTHENTICATION_CONTINUE = 0X0009,
    UNKNOWN_COMMAND = 0X0081,
    OUT_OF_MEMORY = 0X0082,
    NOT_SUPPORTED = 0X0083,
    INTERNAL_ERROR = 0X0084,
    BUSY = 0X0085,
    TEMPORARY_FAILURE = 0X0086
  };

  enum struct VBucketStatus
  {
    ACTIVE = 0x01,
    REPLICA = 0x02,
    PENDING = 0x03,
    DEAD = 0x04
  };

  enum struct Status
  {
    OK,
    DISCONNECTED,
    ERROR,
  };

  /* Binary structure of the fixed-length header for Memcached messages */
  struct MsgHdr
  {
    uint8_t magic;
    uint8_t op_code;
    uint16_t key_length;
    uint8_t extra_length;
    uint8_t data_type;
    uint16_t vbucket_or_status;
    uint32_t body_length;
    uint32_t opaque;
    uint64_t cas;
  };

  /* This abstract base class represents a generic Memcached message.
   *
   * This class is mostly used for defining common utilities and specifying a
   * common API. */
  class BaseMessage
  {
  public:
    BaseMessage(uint8_t op_code, std::string key, uint32_t opaque, uint64_t cas) :
      _op_code(op_code),
      _key(key),
      _opaque(opaque),
      _cas(cas)
    {
    }
    BaseMessage(const std::string& msg);
    virtual ~BaseMessage() {};

    virtual bool is_request() const = 0;
    virtual bool is_response() const = 0;
    inline uint8_t op_code() const { return _op_code; };
    inline const std::string& key() const { return _key; };
    inline uint32_t opaque() const { return _opaque; };
    inline uint64_t cas() const { return _cas; };

    std::string to_wire() const;

  protected:
    virtual std::string generate_extra() const{ return ""; };
    virtual std::string generate_value() const { return ""; };
    virtual uint16_t generate_vbucket_or_status() const = 0;

    uint8_t _op_code;
    std::string _key;
    uint32_t _opaque;
    uint64_t _cas;
  };

  class BaseReq : public BaseMessage
  {
  public:
    BaseReq(uint8_t command,
            std::string key,
            uint16_t vbucket,
            uint32_t opaque,
            uint64_t cas) :
      BaseMessage(command, key, opaque, cas),
      _vbucket(vbucket)
    {
    }
    BaseReq(const std::string& msg);

    bool is_request() const { return true; }
    bool is_response() const { return false; }
    uint16_t vbucket() const { return _vbucket; }

  protected:
    uint16_t generate_vbucket_or_status() const { return _vbucket; }

    uint16_t _vbucket;
  };

  class BaseRsp : public BaseMessage
  {
  public:
    BaseRsp(uint8_t command,
            std::string key,
            uint16_t status,
            uint32_t opaque,
            uint64_t cas) :
      BaseMessage(command, key, opaque, cas),
      _status(status)
    {
    }
    BaseRsp(const std::string& msg);

    bool is_request() const { return false; }
    bool is_response() const { return true; }
    uint16_t result_code() const { return _status; }

  protected:
    uint16_t generate_vbucket_or_status() const { return _status; }

    uint16_t _status;
  };

  class GetReq : public BaseReq
  {
  public:
    GetReq(const std::string& msg) : BaseReq(msg) {}

    // This constructor explicitly takes an opaque parameter to distinguish it
    // from the previous constructor (which initializes a GET request from a
    // message buffer).
    GetReq(std::string key, uint32_t opaque) :
      BaseReq((uint8_t)OpCode::GET, key, 0, opaque, 0)
    {}

    bool response_needs_key() const;
  };

  class GetRsp : public BaseRsp
  {
  public:
    GetRsp(const std::string& msg);
    GetRsp(uint16_t status,
           uint32_t opaque,
           uint64_t cas,
           const std::string& value,
           uint32_t flags,
           const std::string& key = "");

    std::string value() const { return _value; };
    uint32_t flags() const { return _flags; };

  private:
    virtual std::string generate_extra() const;
    virtual std::string generate_value() const;

    std::string _value;
    uint32_t _flags;
  };

  class DeleteReq : public BaseReq
  {
  public:
    DeleteReq(const std::string& msg) : BaseReq(msg) {}

    // This constructor explicitly takes an opaque parameter to distinguish it
    // from the previous constructor (which initializes a GET request from a
    // message buffer).
    DeleteReq(std::string key, uint32_t opaque) :
      BaseReq((uint8_t)OpCode::DELETE, key, 0, opaque, 0)
    {}
  };

  class DeleteRsp : public BaseRsp
  {
  public:
    DeleteRsp(const std::string& msg);
    DeleteRsp(uint8_t status, uint32_t opaque) :
      BaseRsp((uint8_t)OpCode::DELETE, "", status, opaque, 0)
    {}
  };

  class SetAddReplaceReq : public BaseReq
  {
  public:
    SetAddReplaceReq(const std::string& msg);
    SetAddReplaceReq(uint8_t command,
                     std::string key,
                     uint16_t vbucket,
                     std::string value,
                     uint64_t cas,
                     uint32_t flags,
                     uint32_t expiry);

    uint32_t expiry() const { return _expiry; }
    std::string value() const { return _value; }

  protected:
    std::string generate_extra() const;
    std::string generate_value() const;

  private:
    std::string _value;
    uint32_t _flags;
    uint32_t _expiry;
  };

  class SetAddReplaceRsp : public BaseRsp
  {
  public:
    SetAddReplaceRsp(const std::string& msg) : BaseRsp(msg) {};

    SetAddReplaceRsp(uint8_t command,
                     uint8_t status,
                     uint32_t opaque,
                     uint64_t cas = 0) :
      BaseRsp(command, "", status, opaque, cas)
    {};
  };

  class SetReq : public SetAddReplaceReq
  {
  public:
    SetReq(const std::string& msg) : SetAddReplaceReq(msg) {}

    SetReq(std::string key,
           uint16_t vbucket,
           std::string value,
           uint32_t flags,
           uint32_t expiry) :
      SetAddReplaceReq((uint8_t)OpCode::SET, key, vbucket, value, 0, flags, expiry)
    {}
  };

  typedef SetAddReplaceRsp SetRsp;

  class AddReq : public SetAddReplaceReq
  {
  public:
    AddReq(const std::string& msg): SetAddReplaceReq(msg) {}

    AddReq(std::string key,
           uint16_t vbucket,
           std::string value,
           uint32_t flags,
           uint32_t expiry) :
      SetAddReplaceReq((uint8_t)OpCode::ADD, key, vbucket, value, 0, flags, expiry)
    {}
  };

  typedef SetAddReplaceRsp AddRsp;

  class ReplaceReq : public SetAddReplaceReq
  {
  public:
    ReplaceReq(const std::string& msg): SetAddReplaceReq(msg) {}

    ReplaceReq(std::string key,
               uint16_t vbucket,
               std::string value,
               uint64_t cas,
               uint32_t flags,
               uint32_t expiry) :
      SetAddReplaceReq((uint8_t)OpCode::REPLACE, key, vbucket, value, cas, flags, expiry)
    {}
  };

  typedef SetAddReplaceRsp ReplaceRsp;

  class TapConnectReq : public BaseReq
  {
  public:
    TapConnectReq(const VBucketList& buckets);

  protected:
    std::string generate_extra() const;
    std::string generate_value() const;

  private:
    std::vector<uint16_t> _buckets;
  };

  class VersionReq : public BaseReq
  {
  public:
    VersionReq(const std::string& msg) : BaseReq(msg) {}
  };

  class VersionRsp : public BaseRsp
  {
  public:
    VersionRsp(uint16_t status,
               uint32_t opaque,
               const std::string& version);

    std::string generate_value() const;

  private:
    std::string _version;
  };

  class TapMutateReq : public BaseReq
  {
  public:
    TapMutateReq(const std::string& msg);

    std::string value() const { return _value; };
    uint32_t flags() const { return _flags; };
    uint32_t expiry() const { return _expiry; };

  private:
    std::string _value;
    uint32_t _flags;
    uint32_t _expiry;
  };

  class SetVBucketReq : public BaseReq
  {
  public:
    SetVBucketReq(uint16_t vbucket, VBucketStatus status) :
      BaseReq((uint8_t)OpCode::SET_VBUCKET, // Command Code
              "",                           // Key
              vbucket,                      // VBucket
              0,                            // Opaque (unused)
              0),                           // CAS (unused)
      _status(status)
    {
    }

    std::string generate_extra() const;

  private:
    VBucketStatus _status;
  };

  class Connection
  {
  public:
    void disconnect();

    bool send(const BaseMessage& msg);
    Status recv(BaseMessage** msg);

    std::string address() { return _address; }

  protected:
    Connection();
    virtual ~Connection();

    std::string _address;
    int _sock;
    std::string _buffer;
  };

  class ClientConnection : public Connection
  {
  public:
    ClientConnection(const std::string& address);
    int connect();
  };

  class ServerConnection : public Connection
  {
  public:
    ServerConnection(int sock, const std::string& address);
  };

  // Entry point for parsing messages off the wire.
  //
  // @returns  True if the string contains a complete message.
  // @param binary - The message off the wire.  If the message is
  //                 complete, it is removed from the front of the string
  //                 before this function returns.
  // @param output - A pointer to store the parsed message.
  bool from_wire(std::string& binary, BaseMessage*& output);

  // Parsing utility fuctions.
  bool is_msg_complete(const std::string& msg,
                       bool& request,
                       uint32_t& body_length,
                       uint8_t& op_code);
  template <class T> BaseMessage* from_wire_int(const std::string& msg)
  {
    return new T(msg);
  }
};

#endif
