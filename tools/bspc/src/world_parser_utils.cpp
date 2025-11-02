#include "world_parsers.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace bspc::builder
{

std::string EnsureEntitiesText(std::string entities_text)
{
    if (entities_text.empty())
    {
        entities_text = "{\n\"classname\" \"worldspawn\"\n}\n";
        return entities_text;
    }

    if (entities_text.find("worldspawn") == std::string::npos)
    {
        entities_text.append("\n{\n\"classname\" \"worldspawn\"\n}\n");
    }

    if (!entities_text.empty() && entities_text.back() != '\n')
    {
        entities_text.push_back('\n');
    }

    return entities_text;
}

std::vector<std::string> SplitLines(std::string_view text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text)
    {
        if (ch == '\n')
        {
            if (!current.empty() && current.back() == '\r')
            {
                current.pop_back();
            }
            lines.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(ch);
        }
    }

    if (!current.empty())
    {
        lines.push_back(std::move(current));
    }

    return lines;
}

} // namespace bspc::builder

