#pragma once
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

class Config {
public:
    static Config& getInstance();

    bool load(const std::string& filename);

    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    double getDouble(const std::string& key, double defaultValue = 0.0) const;

    // Return the immediate child keys of the YAML map at `prefix`.
    // e.g. getMapKeys("profiles") -> {"yikoulian", "official", "myproxy"}
    // prefix="" returns root-level keys.
    std::vector<std::string> getMapKeys(const std::string& prefix = "") const;

    // Persist a key back to the YAML file loaded by load().
    // Uses dot-separated key syntax ("api.model", "profiles.foo.url") at any depth.
    // Returns false if no file was loaded yet or writing fails.
    bool setString(const std::string& key, const std::string& value);
    bool save();

    int getGpuLayers() const  { return getInt("gpu.layers", -1); }
    int getCpuMoe() const     { return getInt("gpu.cpu_moe", 0); }
    int getBatchSize() const  { return getInt("gpu.batch_size", 512); }
    int getThreads() const    { return getInt("gpu.threads", 0); }

private:
    Config() = default;
    YAML::Node root;
    std::string configPath_;   // path passed to load(); needed by save()

    // Walk the node tree by dot-separated key at arbitrary depth.
    YAML::Node getNode(const std::string& key) const;

    // Write a value into `root` by dot-separated key, creating intermediate maps.
    void setNode(const std::string& key, const std::string& value);
};
