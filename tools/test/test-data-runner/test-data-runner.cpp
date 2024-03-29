#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(WIN32) || defined(WIN64)
#define popen _popen
#define pclose _pclose
#define WEXITSTATUS(x) (x)
#else
#include <unistd.h>
#endif
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <atomic>
#include <vector>
#include <thread>
#include <algorithm>

struct TestData {
    std::string canBlockIsFalse;
    std::string isModule;
    std::string fullPath;
    std::string driverFile;
    std::string testFile;
    std::string code;
    std::string outputFile;
};

enum TestKind {
    General,
    Test262
};

std::string g_inputPath;
std::vector<TestData> g_testDatas;
std::atomic<int> g_index;
std::atomic<int> g_passCount;
std::atomic<int> g_skipCount;
std::vector<std::string> g_skipPattern;
std::string g_env;
bool g_treatGlobalTostringAsObject;
TestKind g_testKind;

std::pair<std::string, int> exec(const std::string& cmd)
{
    char buffer[256];
    std::string result;
    FILE* fp = popen((cmd + " 2>&1").data(), "r");
    if (!fp) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        result += buffer;
    }
    return std::make_pair(result, WEXITSTATUS(pclose(fp)));
}

constexpr const char* ws = " \t\n\r\f\v";

inline std::string& rtrim(std::string& s, const char* t = ws)
{
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

inline std::string& ltrim(std::string& s, const char* t = ws)
{
    s.erase(0, s.find_first_not_of(t));
    return s;
}

inline std::string& trim(std::string& s, const char* t = ws)
{
    return ltrim(rtrim(s, t), t);
}

void replaceAll(std::string &source, const std::string &from, const std::string &to)
{
    std::string newString;
    newString.reserve(source.length());

    std::string::size_type lastPos = 0;
    std::string::size_type findPos;

    while (std::string::npos != (findPos = source.find(from, lastPos))) {
        newString.append(source, lastPos, findPos - lastPos);
        newString += to;
        lastPos = findPos + from.length();
    }

    newString += source.substr(lastPos);

    source.swap(newString);
}


int main(int argc, char* argv[])
{
    std::string shellPath = "escargot";
    int numThread = std::thread::hardware_concurrency();
    g_inputPath = "";
    g_testKind = TestKind::General;

    for (int i = 1; i < argc; i++) {
        if (strlen(argv[i]) >= 2 && argv[i][0] == '-') { // parse command line option
            if (argv[i][1] == '-') { // `--option` case
                if (strcmp(argv[i], "--shell") == 0) {
                    if (argc > i) {
                        shellPath = argv[++i];
                        continue;
                    }
                } else if (strcmp(argv[i], "--test-data") == 0) {
                    if (argc > i) {
                        g_inputPath = argv[++i];
                        continue;
                    }
                } else if (strcmp(argv[i], "--test") == 0) {
                    if (argc > i) {
                        std::string kind = argv[++i];
                        if (kind == "test262") {
                            g_testKind = TestKind::Test262;
                        }
                        continue;
                    }
                } else if (strcmp(argv[i], "--threads") == 0) {
                    if (argc > i) {
                        numThread = std::stoi(argv[++i]);
                        continue;
                    }
                } else if (strcmp(argv[i], "--env") == 0) {
                    if (argc > i) {
                        g_env = argv[++i];
                        continue;
                    }
                } else if (strcmp(argv[i], "--skip") == 0) {
                    if (argc > i) {
                        g_skipPattern.push_back(argv[++i]);
                        continue;
                    }
                } else if (strcmp(argv[i], "--treat-global-tostring-as-object") == 0) {
                    g_treatGlobalTostringAsObject = true;
                    continue;
                }
            } else { // `-option` case
            }
            fprintf(stderr, "Cannot recognize option `%s`", argv[i]);
            continue;
        }
    }

    if (g_inputPath == "") {
        puts("please specifiy test data option with --test-data <path>");
        return -1;
    }

#if !defined(WIN32) && !defined(WIN64)
    if (g_testKind == TestKind::Test262) {
        g_env = "TZ=US/Pacific " + g_env;
    }
#endif

    int caseNum = 0;
    if (g_testKind == TestKind::Test262) {
        std::ifstream input(g_inputPath + "/data");

        std::string canBlockIsFalse;
        std::string isModule;
        std::string fullPath;
        std::string driverFile;
        std::string testFile;
        std::string code;
        std::string outputFile;

        while (std::getline(input, canBlockIsFalse)) {
            std::getline(input, isModule);
            std::getline(input, fullPath);
            std::getline(input, driverFile);
            std::getline(input, testFile);
            std::getline(input, code);
            std::getline(input, outputFile);

            g_testDatas.push_back({
                canBlockIsFalse,
                isModule,
                fullPath,
                driverFile,
                testFile,
                code,
                outputFile
            });
            caseNum++;
        }
    } else {
        std::ifstream input(g_inputPath);

        std::string commandLine;
        std::string code;
        std::string outputFile;
        while (std::getline(input, commandLine)) {
            std::getline(input, code);
            std::getline(input, outputFile);
            g_testDatas.push_back({
                "",
                "",
                "",
                "",
                commandLine,
                code,
                outputFile
            });
            caseNum++;
        }
    }

    printf("Total case number %d\n", caseNum);

    std::vector<std::pair<int, int>> threadData;
    std::vector<std::thread> threads;
    int threadSize = caseNum / numThread;

    int d = 0;
    for (int i = 0; i < numThread; i ++) {
        threadData.push_back(std::make_pair(
            d, d + threadSize
            ));
        d += threadSize;
    }

    threadData.back().second = caseNum;

    for (int i = 0; i < numThread; i ++) {
        threads.push_back(std::thread([](std::pair<int, int> data, std::string shellPath) {
            for (int j = data.first; j < data.second; j ++) {
                std::string commandline = g_env + " " + shellPath;
                const auto& data = g_testDatas[j];

                commandline += " " + data.driverFile;
                if (data.canBlockIsFalse.size()) {
                    commandline += " --canblock-is-false";
                }

                if (data.isModule.size()) {
                    commandline += " --module";
                    commandline += " --filename-as=" + g_inputPath + data.fullPath;
                }

                commandline += " " + data.testFile;

                std::string info = data.fullPath.size() ? data.fullPath : commandline;

                bool skip = false;
                for (const auto& skipPattern : g_skipPattern) {
                    if (data.fullPath.find(skipPattern) != std::string::npos) {
                        skip = true;
                        break;
                    }
                }

                if (skip) {
                    g_skipCount++;
                    printf("SKIP [%d] %s\n", g_index++, info.data());
                    continue;
                }

                auto result = exec(commandline);

                bool outputTest = true;
                if (data.outputFile.size()) {
                    std::ifstream input(data.outputFile);
                    std::string outputContents;
                    std::string line;
                    while (std::getline(input, line)) {
                        if (outputContents.size()) {
                            outputContents += "\n";

                        }
                        outputContents += line;
                    }

                    std::string org = outputContents;
                    trim(outputContents);
                    trim(result.first);

                    replaceAll(outputContents, "\r","");
                    replaceAll(result.first, "\r","");

                    if (g_treatGlobalTostringAsObject) {
                        replaceAll(result.first, "[object global]","[object Object]");
                    }

                    outputTest = outputContents == result.first;
                }

                if (data.code == std::to_string(result.second) && outputTest) {
                    g_passCount++;
                    printf("Success [%d] %s => %d\n", g_index++, info.data(), result.second);
                } else {
                    printf("Fail [%d] %s\n", g_index++, commandline.data());
                    printf("Fail output->\n%s\n", result.first.data());
                }
            }
        }, threadData[i], shellPath));
    }

    for (int i = 0; i < numThread; i ++) {
        threads[i].join();
    }

    printf("Result -> %d/%d(Skipped %d) : ", ((int)g_passCount + (int)g_skipCount), caseNum, (int)g_skipCount);
    if ((g_passCount + g_skipCount) == caseNum) {
        puts("Passed");
    } else {
        puts("Failed");
    }

    return 0;
}
