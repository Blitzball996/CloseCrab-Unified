#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>

namespace closecrab {
class LocalDesktopBridge {
public:
    struct Answer { int index = -1; std::string text; };
    LocalDesktopBridge() = default;
    ~LocalDesktopBridge() { stop(); }
    LocalDesktopBridge(const LocalDesktopBridge&) = delete;
    LocalDesktopBridge& operator=(const LocalDesktopBridge&) = delete;
    static LocalDesktopBridge* active() { return active_; }
    bool start(std::function<void()> interrupt) {
        const char* port = std::getenv("CLOSECRAB_DESKTOP_PORT");
        const char* token = std::getenv("CLOSECRAB_DESKTOP_TOKEN");
        if (!port || !token || std::string(token).size() < 32) return false;
        int number;
        try { number = std::stoi(port); } catch (...) { return false; }
        if (number < 1024 || number > 65535) return false;
        token_ = token; interrupt_ = std::move(interrupt);
        server_ = std::make_unique<ix::WebSocketServer>(number, "127.0.0.1");
        server_->setOnClientMessageCallback([this](std::shared_ptr<ix::ConnectionState>, ix::WebSocket& ws, const ix::WebSocketMessagePtr& m) {
            try {
                if (m->type == ix::WebSocketMessageType::Open) { std::lock_guard<std::mutex> g(mutex_); clients_[&ws] = false; }
                else if (m->type == ix::WebSocketMessageType::Close) {
                    std::lock_guard<std::mutex> g(mutex_); clients_.erase(&ws);
                    if (std::none_of(clients_.begin(), clients_.end(), [](const auto& c){return c.second;})) { choiceCancelled_=true; cv_.notify_all(); }
                } else if (m->type == ix::WebSocketMessageType::Message) receive(ws,m->str);
            } catch (...) { ws.close(1008,"Invalid desktop message"); }
        });
        if (!server_->listen().first) { server_.reset(); return false; }
        server_->start(); active_=this;
#ifdef _WIN32
        _putenv_s("CLOSECRAB_DESKTOP_TOKEN", ""); _putenv_s("CLOSECRAB_DESKTOP", "");
#else
        unsetenv("CLOSECRAB_DESKTOP_TOKEN"); unsetenv("CLOSECRAB_DESKTOP");
#endif
        return true;
    }
    void stop() { { std::lock_guard<std::mutex> g(mutex_); stopped_=true; choiceCancelled_=true; cv_.notify_all(); } if(server_)server_->stop(); if(active_==this)active_=nullptr; server_.reset(); }
    void publish(nlohmann::json event) noexcept { if(!server_)return; try { std::lock_guard<std::mutex> g(mutex_); publishLocked(std::move(event)); } catch(...){} }
    void ready(const std::string& session,const std::string& model,const std::string& cwd) { if(!server_)return; std::lock_guard<std::mutex> g(mutex_); ready_=true; publishLocked({{"type","ready"},{"sessionId",session},{"model",model},{"cwd",cwd}}); }
    bool takeInput(std::string& input) { std::lock_guard<std::mutex> g(mutex_); ready_=false; if(!pendingInput_)return false; input=std::move(*pendingInput_); pendingInput_.reset(); return true; }
    bool connected() { std::lock_guard<std::mutex> g(mutex_); return std::any_of(clients_.begin(),clients_.end(),[](const auto& c){return c.second;}); }
    Answer ask(const std::string& title,const std::vector<std::string>& options,bool custom) {
        std::unique_lock<std::mutex> serial(choiceMutex_);
        std::unique_lock<std::mutex> g(mutex_);
        const bool permission=options==std::vector<std::string>{"Allow","Deny","Allow All"};
        const auto id=std::to_string(++requestSequence_); choiceId_=id; choiceOptions_=options.size(); choiceCustom_=custom; choiceAnswered_=false; choiceCancelled_=false;
        publishLocked({{"type","choice"},{"requestId",id},{"title",title},{"options",options},{"allowCustom",custom},{"permission",permission}});
        cv_.wait_for(g,std::chrono::minutes(5),[this]{return stopped_||choiceCancelled_||choiceAnswered_;});
        Answer a=choiceAnswered_&&!choiceCancelled_?answer_:Answer{permission?1:-1,""}; choiceId_.clear();
        publishLocked({{"type","choice_resolved"},{"requestId",id},{"index",a.index}});return a;
    }
private:
    void publishLocked(nlohmann::json event) {
        event["seq"]=++sequence_; event["version"]=1;
        std::string s=event.dump(-1,' ',false,nlohmann::json::error_handler_t::replace);
        if(s.size()>1024*1024)s=nlohmann::json{{"type","oversized_event"},{"seq",sequence_},{"version",1}}.dump();
        backlogBytes_+=s.size(); backlog_.emplace_back(sequence_,s);
        while(backlog_.size()>4096||backlogBytes_>8*1024*1024){backlogBytes_-=backlog_.front().second.size();backlog_.pop_front();}
        for(auto& c:clients_)if(c.second)c.first->send(s);
    }
    void receive(ix::WebSocket& ws,const std::string& text) {
        if(text.size()>1024*1024){ws.close(1009,"Message too large");return;}
        auto m=nlohmann::json::parse(text); const auto type=m.value("type","");
        std::unique_lock<std::mutex> g(mutex_);
        if(!clients_[&ws]){
            if(type!="auth"||m.value("token","")!=token_){g.unlock();ws.close(1008,"Authentication required");return;}
            clients_[&ws]=true; const uint64_t after=m.value("after",uint64_t(0));
            ws.send(nlohmann::json{{"type","connected"},{"version",1},{"ready",ready_},{"oldest",backlog_.empty()?sequence_:backlog_.front().first}}.dump());
            for(const auto& e:backlog_)if(e.first>after)ws.send(e.second);return;
        }
        if(type=="prepare_input"){
            const auto rid=m.value("requestId","");const auto value=m.value("content","");
            if(!ready_||pendingInput_||!choiceId_.empty()||value.empty()){ws.send(nlohmann::json{{"type","input_rejected"},{"requestId",rid}}.dump());return;}
            ready_=false;pendingInput_=value;ws.send(nlohmann::json{{"type","input_prepared"},{"requestId",rid}}.dump());
        }else if(type=="answer"){
            int i=m.value("index",-2);auto value=m.value("text","");
            if(choiceId_.empty()||choiceAnswered_||m.value("requestId","")!=choiceId_)return;
            if(!((i>=0&&static_cast<size_t>(i)<choiceOptions_)||(i==-1&&choiceCustom_))||value.size()>65536)return;
            answer_={i,value};choiceAnswered_=true;cv_.notify_all();
        }else if(type=="interrupt"){choiceCancelled_=true;cv_.notify_all();g.unlock();if(interrupt_)interrupt_();}
    }
    inline static LocalDesktopBridge* active_=nullptr;
    std::unique_ptr<ix::WebSocketServer> server_;
    std::map<ix::WebSocket*,bool> clients_;
    std::mutex mutex_,choiceMutex_;std::condition_variable cv_;std::function<void()> interrupt_;
    std::string token_,choiceId_;std::optional<std::string> pendingInput_;
    std::deque<std::pair<uint64_t,std::string>> backlog_;
    size_t backlogBytes_=0,choiceOptions_=0;uint64_t sequence_=0,requestSequence_=0;
    bool ready_=false,stopped_=false,choiceAnswered_=false,choiceCancelled_=false,choiceCustom_=false;
    Answer answer_;
};
}
