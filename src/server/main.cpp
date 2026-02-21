#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>

#include <grpcpp/grpcpp.h>
#include "proto/sandbox.grpc.pb.h"
#include "src/server/sandbox.h"
#include "src/server/logger.h"

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerWriteReactor;
using grpc::Status;
using grpc::StatusCode;
using dcodex::CodeExecutor;
using dcodex::CodeRequest;
using dcodex::ExecutionLog;
using dcodex::Sandbox;
using dcodex::Logger;
using dcodex::LogLevel;

class CodeExecutorServiceImpl final : public CodeExecutor::CallbackService {
public:
    CodeExecutorServiceImpl() : active_sandboxes_(0) {}

    ServerWriteReactor<ExecutionLog>* Execute(CallbackServerContext* context,
                                             const CodeRequest* request) override {
        int active = active_sandboxes_.fetch_add(1);
        Logger::Info("Received Execute request. Active sandboxes: ", active + 1);

        if (active >= 10) {
            active_sandboxes_.fetch_sub(1);
            Logger::Warn("Too many active sandboxes. Rejecting request.");
            class RejectReactor : public ServerWriteReactor<ExecutionLog> {
            public:
                RejectReactor() {
                    Finish(Status(StatusCode::RESOURCE_EXHAUSTED, "Too many active sandboxes"));
                }
                void OnDone() override { delete this; }
            };
            return new RejectReactor();
        }

        class ExecuteReactor : public ServerWriteReactor<ExecutionLog> {
        public:
            ExecuteReactor(const CodeRequest* request, std::atomic<int>& counter) 
                : request_(request), finished_(false), writing_(false), counter_(counter) {
                
                // Start worker thread
                worker_thread_ = std::thread([this]() {
                    Logger::Info("Starting execution for language: ", request_->language());
                    
                    auto result = Sandbox::Execute(
                        request_->language(),
                        request_->code(), 
                        [this](const std::string& stdout_chunk, const std::string& stderr_chunk) {
                            std::lock_guard<std::mutex> lock(mutex_);
                            ExecutionLog log;
                            if (!stdout_chunk.empty()) log.set_stdout_chunk(stdout_chunk);
                            if (!stderr_chunk.empty()) log.set_stderr_chunk(stderr_chunk);
                            queue_.push(log);
                            MaybeWriteNext();
                        });
                    
                    std::lock_guard<std::mutex> lock(mutex_);
                    finished_ = true;
                    if (!result.success) {
                        Logger::Warn("Execution finished with error: ", result.error_message);
                        // Optionally send the error as a final log or status?
                        // For now, let's just finish the stream.
                    } else {
                        Logger::Info("Execution finished successfully.");
                    }
                    MaybeWriteNext();
                });
            }

            void OnWriteDone(bool ok) override {
                std::lock_guard<std::mutex> lock(mutex_);
                writing_ = false;
                if (!ok) {
                    Logger::Warn("Client disconnected or write failed.");
                    // Don't stop worker immediately, let it finish or use cancellation token if implemented
                    return;
                }
                MaybeWriteNext();
            }

            void OnDone() override {
                Logger::Info("RPC OnDone called.");
                if (worker_thread_.joinable()) {
                    worker_thread_.join();
                }
                counter_.fetch_sub(1);
                delete this;
            }

            void OnCancel() override {
                Logger::Warn("RPC Cancelled by client.");
                // In a real system, we'd signal the worker thread to stop the process
            }

        private:
            void MaybeWriteNext() {
                if (writing_) return;

                if (!queue_.empty()) {
                    writing_ = true;
                    current_log_ = queue_.front();
                    queue_.pop();
                    StartWrite(&current_log_);
                } else if (finished_) {
                    Finish(Status::OK);
                }
            }

            const CodeRequest* request_;
            std::thread worker_thread_;
            std::mutex mutex_;
            std::queue<ExecutionLog> queue_;
            ExecutionLog current_log_;
            bool finished_;
            bool writing_;
            std::atomic<int>& counter_;
        };

        return new ExecuteReactor(request, active_sandboxes_);
    }

private:
    std::atomic<int> active_sandboxes_;
};

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    CodeExecutorServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    Logger::Info("Server listening on ", server_address);
    server->Wait();
}

int main(int argc, char** argv) {
    RunServer();
    return 0;
}
