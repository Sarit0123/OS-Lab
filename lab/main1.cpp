// Lab 1: Computer Information
// แสดงข้อมูลคอมพิวเตอร์บนระบบ Linux โดยอ่านจาก /proc และใช้ POSIX API

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mntent.h>
#include <set>
#include <sys/stat.h>
#include <sstream>
#include <string>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string humanSize(unsigned long long bytes) {
    static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << value << ' ' << units[unit];
    return out.str();
}

void printTitle(const std::string& title) {
    std::cout << "\n===== " << title << " =====\n";
}

// 1. ชื่อ CPU
std::string cpuName() {
    std::ifstream file("/proc/cpuinfo");
    std::string line;
    while (std::getline(file, line)) {
        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, separator));
        if (key == "model name" || key == "Hardware" || key == "Processor" || key == "cpu model") {
            const std::string value = trim(line.substr(separator + 1));
            if (!value.empty() && value != "0") {
                return value;
            }
        }
    }

    struct utsname info{};
    if (uname(&info) == 0) {
        return std::string(info.machine) + " (ไม่พบชื่อรุ่น CPU ใน /proc/cpuinfo)";
    }
    return "ไม่ทราบชื่อ CPU";
}

// 2. หน่วยความจำ
void showMemory() {
    unsigned long long totalKb = 0;
    unsigned long long availableKb = 0;

    std::ifstream file("/proc/meminfo");
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string key;
        unsigned long long valueKb = 0;
        if (!(fields >> key >> valueKb)) {
            continue;
        }
        if (key == "MemTotal:") {
            totalKb = valueKb;
        } else if (key == "MemAvailable:") {
            availableKb = valueKb;
        }
    }

    if (totalKb == 0) {  // สำรองเมื่อไม่มี /proc/meminfo
        struct sysinfo info{};
        if (sysinfo(&info) == 0) {
            totalKb = static_cast<unsigned long long>(info.totalram) / 1024;
            availableKb = static_cast<unsigned long long>(info.freeram + info.bufferram) / 1024;
        }
    }

    const unsigned long long usedKb = totalKb >= availableKb ? totalKb - availableKb : 0;
    const double usedPercent = totalKb ? 100.0 * usedKb / totalKb : 0.0;

    std::cout << " RAM ทั้งหมด     : " << humanSize(totalKb * 1024) << '\n';
    std::cout << " RAM ที่ใช้ได้   : " << humanSize(availableKb * 1024) << '\n';
    std::cout << " RAM ที่ถูกใช้   : " << humanSize(usedKb * 1024) << " (" << std::fixed
              << std::setprecision(1) << usedPercent << "%)\n";
}

// 3. ชื่อและเวอร์ชันของระบบปฏิบัติการ
std::string osReleaseValue(const std::string& wanted) {
    std::ifstream file("/etc/os-release");
    std::string line;
    while (std::getline(file, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos || trim(line.substr(0, separator)) != wanted) {
            continue;
        }
        std::string value = trim(line.substr(separator + 1));
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    }
    return "";
}

void showOperatingSystem() {
    std::string distribution = osReleaseValue("PRETTY_NAME");
    if (distribution.empty()) {
        const std::string name = osReleaseValue("NAME");
        const std::string version = osReleaseValue("VERSION_ID");
        distribution = name + (version.empty() ? "" : " " + version);
    }

    struct utsname info{};
    if (uname(&info) == 0) {
        std::cout << " ระบบปฏิบัติการ : " << (distribution.empty() ? "ไม่ทราบชื่อ" : distribution) << '\n';
        std::cout << " Kernel         : " << info.sysname << ' ' << info.release << '\n';
        std::cout << " Kernel version : " << info.version << '\n';
        std::cout << " สถาปัตยกรรม    : " << info.machine << '\n';
    } else {
        std::cout << " ระบบปฏิบัติการ : " << (distribution.empty() ? "ไม่ทราบชื่อ" : distribution) << '\n';
    }
}

struct Filesystem {
    std::string device;
    std::string mountPoint;
    unsigned long long total;
    unsigned long long used;
    unsigned long long available;
};

// 4. พื้นที่จัดเก็บข้อมูล
std::vector<Filesystem> listFilesystems() {
    std::vector<Filesystem> result;
    std::map<std::string, std::size_t> seenByIdentity;  // fsid -> index in result

    FILE* mounts = setmntent("/proc/self/mounts", "r");
    if (mounts != nullptr) {
        mntent entry{};
        char buffer[4096];
        while (getmntent_r(mounts, &entry, buffer, sizeof(buffer)) != nullptr) {
            const std::string type = entry.mnt_type;
            static const std::set<std::string> virtualTypes = {
                "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "cgroup", "cgroup2",
                "securityfs", "pstore", "debugfs", "tracefs", "hugetlbfs", "mqueue",
                "fusectl", "configfs", "binfmt_misc", "nsfs", "autofs", "ramfs"};
            if (virtualTypes.count(type)) {
                continue;
            }

            // ข้าม mount ที่เป็นไฟล์เดี่ยว (เช่น /etc/hosts) เพราะไม่ใช่พื้นที่จัดเก็บ
            struct stat mountStat{};
            if (stat(entry.mnt_dir, &mountStat) != 0 || !S_ISDIR(mountStat.st_mode)) {
                continue;
            }

            struct statvfs stat{};
            if (statvfs(entry.mnt_dir, &stat) != 0 || stat.f_blocks == 0) {
                continue;
            }

            const unsigned long long total =
                static_cast<unsigned long long>(stat.f_blocks) * stat.f_frsize;
            const unsigned long long freeBytes =
                static_cast<unsigned long long>(stat.f_bfree) * stat.f_frsize;
            const unsigned long long available =
                static_cast<unsigned long long>(stat.f_bavail) * stat.f_frsize;

            // bind mount หลายจุดอาจชี้ filesystem เดียวกัน ให้แสดงเพียงจุดหลัก
            const std::string identity = std::to_string(stat.f_fsid);
            const Filesystem filesystem = {entry.mnt_fsname, entry.mnt_dir, total,
                                           total - freeBytes, available};
            const auto existing = seenByIdentity.find(identity);
            if (existing == seenByIdentity.end()) {
                seenByIdentity[identity] = result.size();
                result.push_back(filesystem);
            } else if (filesystem.mountPoint.size() <
                       result[existing->second].mountPoint.size()) {
                result[existing->second] = filesystem;
            }
        }
        endmntent(mounts);
    }

    if (result.empty()) {  // สำรองเมื่ออ่านรายการ mount ไม่ได้
        struct statvfs stat{};
        if (statvfs("/", &stat) == 0) {
            const unsigned long long total =
                static_cast<unsigned long long>(stat.f_blocks) * stat.f_frsize;
            const unsigned long long freeBytes =
                static_cast<unsigned long long>(stat.f_bfree) * stat.f_frsize;
            result.push_back({"root", "/", total, total - freeBytes,
                              static_cast<unsigned long long>(stat.f_bavail) * stat.f_frsize});
        }
    }

    std::sort(result.begin(), result.end(),
              [](const Filesystem& lhs, const Filesystem& rhs) {
                  return lhs.mountPoint < rhs.mountPoint;
              });
    return result;
}

void showStorage() {
    const auto filesystems = listFilesystems();
    std::cout << std::left << std::setw(22) << "Device" << std::setw(12) << "Size"
              << std::setw(12) << "Used" << std::setw(12) << "Avail" << std::setw(7)
              << "Use%" << "Mounted on\n";
    std::cout << std::string(78, '-') << '\n';

    for (const auto& fs : filesystems) {
        const unsigned long long usable = fs.used + fs.available;
        const double percent = usable ? 100.0 * fs.used / usable : 0.0;
        std::ostringstream percentText;
        percentText << std::fixed << std::setprecision(1) << percent << "%";
        std::cout << std::left << std::setw(22) << fs.device.substr(0, 21) << std::setw(12)
                  << humanSize(fs.total) << std::setw(12) << humanSize(fs.used)
                  << std::setw(12) << humanSize(fs.available) << std::setw(7)
                  << percentText.str() << fs.mountPoint << '\n';
    }
}

struct Process {
    pid_t pid;
    std::string name;
    char state;
    unsigned long long rssKiB;
};

bool isDigits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char ch) {
               return std::isdigit(ch) != 0;
           });
}

// 5. Process ที่ทำงานอยู่ในระบบ
std::vector<Process> listProcesses() {
    std::vector<Process> processes;
    DIR* directory = opendir("/proc");
    if (directory == nullptr) {
        return processes;
    }

    dirent* entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        if (!isDigits(entry->d_name)) {
            continue;
        }

        const std::string pidText = entry->d_name;
        std::ifstream statFile("/proc/" + pidText + "/stat");
        std::string line;
        if (!std::getline(statFile, line)) {
            continue;  // process อาจจบระหว่างอ่าน
        }

        const auto nameBegin = line.find('(');
        const auto nameEnd = line.rfind(')');
        if (nameBegin == std::string::npos || nameEnd == std::string::npos || nameEnd < nameBegin) {
            continue;
        }

        std::istringstream fields(line.substr(nameEnd + 1));
        std::string state;
        std::vector<std::string> values;
        std::string token;
        while (fields >> token) {
            values.push_back(token);
        }
        if (values.empty()) {
            continue;
        }
        state = values[0];  // field ที่ 3 ของ /proc/[pid]/stat

        unsigned long long rssPages = 0;
        if (values.size() > 21) {  // field ที่ 24 (rss) นับจาก state
            try {
                rssPages = std::stoull(values[21]);
            } catch (...) {
                rssPages = 0;
            }
        }

        processes.push_back({static_cast<pid_t>(std::stoi(pidText)),
                             line.substr(nameBegin + 1, nameEnd - nameBegin - 1),
                             state[0], rssPages * static_cast<unsigned long long>(sysconf(_SC_PAGESIZE)) / 1024});
    }
    closedir(directory);

    std::sort(processes.begin(), processes.end(),
              [](const Process& lhs, const Process& rhs) { return lhs.pid < rhs.pid; });
    return processes;
}

void showProcesses() {
    const auto processes = listProcesses();
    std::cout << " จำนวน process ทั้งหมด : " << processes.size() << '\n';
    std::cout << std::left << std::setw(10) << "PID" << std::setw(9) << "STATE"
              << std::setw(12) << "RSS" << "COMMAND\n";
    std::cout << std::string(60, '-') << '\n';
    for (const auto& process : processes) {
        std::cout << std::left << std::setw(10) << process.pid << std::setw(9) << process.state
                  << std::setw(12) << humanSize(process.rssKiB * 1024) << process.name << '\n';
    }
}

}  // namespace

int main() {
    std::cout << "Lab 1: Computer Information\n";
    std::cout << std::string(30, '=') << '\n';

    printTitle("1. CPU");
    std::cout << " ชื่อ CPU       : " << cpuName() << '\n';
    std::cout << " จำนวน core    : " << sysconf(_SC_NPROCESSORS_ONLN) << '\n';

    printTitle("2. Memory");
    showMemory();

    printTitle("3. Operating System");
    showOperatingSystem();

    printTitle("4. Storage");
    showStorage();

    printTitle("5. Processes");
    showProcesses();

    return 0;
}
