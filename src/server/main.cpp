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

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerWriteReactor;
using grpc::Status;
using grpc::StatusCode;
using dcodex::CodeExecutor;
using dcodex::CodeRequest;
using dcodex::ExecutionLog;
using dcodex::SandboxedProcess;

class CodeExecutorServiceImpl final : public CodeExecutor::CallbackService {
public:
    CodeExecutorServiceImpl() : active_sandboxes_(0) {}

    ServerWriteReactor<ExecutionLog>* Execute(CallbackServerContext* context,
                                             const CodeRequest* request) override {
        if (active_sandboxes_.fetch_add(1) >= 10) {
            active_sandboxes_.fetch_sub(1);
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
                worker_thread_ = std::thread([this]() {
                    SandboxedProcess::CompileAndRunStreaming(request_->code(), 
                        [this](const std::string& stdout_chunk, const std::string& stderr_chunk) {
                            std::lock_guard<std::mutex> lock(mutex_);
                            ExecutionLog log;
                            log.set_stdout_chunk(stdout_chunk);
                            log.set_stderr_chunk(stderr_chunk);
                            queue_.push(log);
                            MaybeWriteNext();
                        });
                    
                    std::lock_guard<std::mutex> lock(mutex_);
                    finished_ = true;
                    MaybeWriteNext();
                });
            }

            void OnWriteDone(bool ok) override {
                std::lock_guard<std::mutex> lock(mutex_);
                writing_ = false;
                if (!ok) {
                    // Handle error, maybe stop worker?
                    return;
                }
                MaybeWriteNext();
            }

            void OnDone() override {
                if (worker_thread_.joinable()) {
                    worker_thread_.join();
                }
                counter_.fetch_sub(1);
                delete this;
            }

            void OnCancel() override {
                // In a real system, we'd kill the process here
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
    std::cout << "Server listening on " << server_address << std::endl;
    server->Wait();
}

int main(int argc, char** argv) {
    RunServer();
    return 0;
}