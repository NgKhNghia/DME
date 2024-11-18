// node.h
#ifndef NODE_H
#define NODE_H

#include <string>
#include <fstream>
#include <map>
#include <thread>
#include "comm.h"
#include "config.h"
#include "log.h"

extern Config config;
extern Logger logger;


class Node {
protected:
    int id;
    std::string ip;
    int port;
    std::shared_ptr<Comm<std::string>> comm; 

public:
    Node(int id, const std::string &ip, int port, std::shared_ptr<Comm<std::string>> comm)
        : id(id), ip(ip), port(port), comm(comm) {}
    
    virtual ~Node() = default;
    virtual void initialize() = 0;
};

class TokenBasedNode : public Node {
protected:
    bool token;

public:
    TokenBasedNode(int id, const std::string &ip, int port, std::shared_ptr<Comm<std::string>> comm)
        : Node(id, ip, port, comm), token(false) {}

    virtual ~TokenBasedNode() = default;
    virtual void requestToken() = 0; 
    virtual void releaseToken() = 0;

};

class PermissonBasedNode : public Node {
public:
    PermissonBasedNode(int id, const std::string &ip, int port, std::shared_ptr<Comm<std::string>> comm)
        : Node(id, ip, port, comm) {}

    virtual ~PermissonBasedNode() = default;
};

#endif // NODE_H
