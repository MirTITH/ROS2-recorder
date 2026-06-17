#include <gtest/gtest.h>

#include <cstdio>
#include <sstream>
#include <string>

#include <sys/wait.h>

namespace
{

struct CommandResult
{
  int exit_code{-1};
  std::string output;
};

CommandResult run_missing_config()
{
  const std::string command =
    std::string("\"") + DATA_RECORDER_EXECUTABLE_PATH + "\" 2>&1";
  FILE * pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return {-1, ""};
  }

  std::ostringstream output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output << buffer;
  }

  const int status = pclose(pipe);
  if (WIFEXITED(status)) {
    return {WEXITSTATUS(status), output.str()};
  }
  return {-1, output.str()};
}

}  // namespace

TEST(UsageHelp, MissingConfigShowsCurrentExamplePath)
{
  const auto result = run_missing_config();

  EXPECT_EQ(result.exit_code, 1) << result.output;
  EXPECT_NE(
    result.output.find(
      "/home/nros/Documents/Woosh/ros2_recorder_ws/src/data_recorder/config/example_config.yaml"),
    std::string::npos)
    << result.output;
  EXPECT_EQ(result.output.find("/docs/reference/example_config.yaml"), std::string::npos)
    << result.output;
}
