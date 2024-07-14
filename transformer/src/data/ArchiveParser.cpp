#include "ArchiveParser.hpp"
#include "spdlog/spdlog.h"
#include <pugixml.hpp>

#include <archive.h>
#include <archive_entry.h>
#include <iostream>
#include <stdexcept>
#include <stc/StringUtil.hpp>
#include <cassert>

namespace sedd {

ArchiveParser::ArchiveParser(const std::filesystem::path& path) 
    : archivePath(path), outputPath(
        archivePath.parent_path() / "output"
    ),
    a(archive_read_new())
{
    archive_read_support_format_7zip(a);
    archive_read_support_filter_all(a);
    
    int r = archive_read_open_filename(a, path.c_str(), BLOCK_SIZE);
    if (r != ARCHIVE_OK) {
        std::cerr << "Failed to read archive with error code = " << r << ": " << archive_error_string(a) << std::endl;
        throw std::runtime_error("Failed to read archive");
    }

}

ArchiveParser::~ArchiveParser() {
    archive_read_free(a);
}

void ArchiveParser::read() {
    archive_entry *entry;


    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        std::string entryName = archive_entry_pathname(entry);
        std::cout << "Extracting " << entryName << std::endl;

        size_t readSize;
        la_int64_t offset;

        const void* buff;
        std::string openingTag = "";

        std::string incompleteBlock = "";
        // Incrementally read the data
        while (true) {
            int r = archive_read_data_block(a, &buff, &readSize, &offset);
            if (r == ARCHIVE_EOF) {
                break;
            } else if (r != ARCHIVE_OK) {
                std::cerr << "Error reading data: " << archive_error_string(a) << std::endl;
                throw std::runtime_error("Failed to read data");
            }
            std::string block(static_cast<const char*>(buff), readSize);
            std::string blockWithPrevious = incompleteBlock + block;
            incompleteBlock = "";

            bool completeBlock = blockWithPrevious.back() == '\n';

            // For some reason, the archive uses Windows CRLF
            std::vector<std::string> lines = stc::string::split(blockWithPrevious, "\r\n");
            // If the block is incomplete
            if (!completeBlock) {
                incompleteBlock = lines.back();
                if (incompleteBlock.starts_with("</")) {
                    // We found a closing tag; this is always the root tag being closed
                    incompleteBlock = "";
                } else {
                    lines.pop_back();
                }
            }

            for (const auto& line : lines) {
                if (line.size() == 0) {
                    continue;
                }
                auto openIdx = line.find('<');
                if (openIdx == std::string::npos || line.starts_with("</")) {
                    continue;
                }
                if (line.substr(openIdx).starts_with("<row")) {
                    if (openingTag == "") {
                        std::cerr << "Failed to parse opening tag" << std::endl;
                        throw std::runtime_error("Failed to find opening tag before row content");
                    }

                    pugi::xml_document doc;
                    pugi::xml_parse_result res = doc.load_string(line.c_str());
                    if (!res) {
                        std::cerr << "Failed to parse line as XML: " << line << "\nReason: " << res.description() << std::endl;
                        throw std::runtime_error("Failed to parse line as XML");
                    }
                    const auto& node = doc.first_child();
                    for (pugi::xml_attribute attr : node.attributes()) {
                        spdlog::debug("{} = {}", attr.name(), attr.value());
                    }

                    // TODO: forward to transformer
                } else if (openingTag == "") {
                    for (const auto& tag : KNOWN_TAGS) {
                        if (line == "<" + tag + ">") {
                            openingTag = tag;
                            spdlog::debug("Found opening tag {}", tag);
                            break;
                        }
                    }
                } else {
                    spdlog::warn("Unknown line: {}", line);
                }
            }

        }

        if (incompleteBlock != "") {
            std::cerr << "Failed to fully parse file; found trailing block past EOF: " << incompleteBlock << std::endl;
            throw std::runtime_error("Failed to fully parse file");
        }

    }
}

}
