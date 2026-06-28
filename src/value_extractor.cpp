#include "data_recorder/value_extractor.hpp"

namespace data_recorder
{

void ValueExtractorRegistry::register_extractor(
  const std::string & type, std::unique_ptr<ValueExtractor> extractor)
{
  extractors_[type] = std::move(extractor);
}

bool ValueExtractorRegistry::has(const std::string & type) const
{
  return extractors_.find(type) != extractors_.end();
}

const ValueExtractor * ValueExtractorRegistry::get(const std::string & type) const
{
  auto it = extractors_.find(type);
  return it == extractors_.end() ? nullptr : it->second.get();
}

void register_builtin_extractors(ValueExtractorRegistry &)
{
  // Task 2 填充。
}

}  // namespace data_recorder
