#pragma once 

namespace tanyatik {

template<typename ProtocolHandler>
class AsyncInputHandler {
private:
    IODescriptor &descriptor_;
    ProtocolHandler protocol_;

    static constexpr size_t BUFFER_SIZE = 512;

public:
    AsyncInputHandler(IODescriptor &descriptor, ProtocolHandler protocol) :
        descriptor_(descriptor),
        protocol_(protocol)
        {}

    bool handleInput() {
        while (true) {
            char buffer[BUFFER_SIZE];
            memset(buffer, 0, BUFFER_SIZE);

            auto recv_result = ::recv(descriptor_.getDescriptor(), buffer, sizeof(buffer), 0);
            if (recv_result > 0) {
                // read successful
                char *buffer_end = buffer + recv_result;
                std::vector<char> data_chunk(buffer, buffer_end);

                return protocol_.processDataChunk(data_chunk);
            } else if (recv_result < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    // end data portion
                    return false;
                } else {
                    throw std::runtime_error("recv failed");
                }
            } else {
                // connection closed
                // free descriptor
                descriptor_.close();
                return true;
            }
        }
    }
};

template<typename ProtocolHandler>
class AsyncOutputHandler {
private:
    IODescriptor &descriptor_;
    std::vector<char> buffer_;
    ProtocolHandler protocol_;

public:
    AsyncOutputHandler(IODescriptor &descriptor, ProtocolHandler protocol) :
        descriptor_(descriptor),
        protocol_(protocol)
        {}

    void handleOutput() {
        if (buffer_.empty()) {
            buffer_ = protocol_.getRespond();
        }

        auto result = ::write(descriptor_.getDescriptor(), buffer_.data(), buffer_.size());
        if (result == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // remembered this buffer, will try to put it next time
                return; 
            }
            throw std::runtime_error("write failed");
        }
        buffer_ = std::vector<char>();
    }
};

} // namespace tanyatik
