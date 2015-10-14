local headerBegin = "--[["
local headerEnd = "--]]"

local ffiBlockBegin = "ffi.cdef[["
local ffiBlockEnd = "]]"

local sourceListPlaceHolder = "<source%-files>" 
-- a hyphen is a special character so it must be preceded by "%" escape character 

local outputOption = "--output="
local outputFileName = "out.lua" -- default output file name

local headerOption = "--header="
local headerFileName = ""
local footerOption = "--footer="
local footerFileName = ""

local inputFiles = {} -- list of files to combine
local bindings = {} -- map of bindings
local output = {} -- list of lines to write to the output file
local sourceFiles = {} -- list of source files that given lua files are based on

local sourcesLineIndex = -1

-- this function determines whether the specified table contains the given element
function table.contains(table, element)
  for _, value in pairs(table) do
    if value == element then
      return true
    end
  end
  return false
end

-- get names of the lua files to combine (and if set, output, header and footer file names)
for i = 1, #arg do
  if string.sub(arg[i], 1, string.len(outputOption)) == outputOption then
    outputFileName = string.sub(arg[i], string.len(outputOption) + 1)
  elseif string.sub(arg[i], 1, string.len(headerOption)) == headerOption then
    headerFileName = string.sub(arg[i], string.len(headerOption) + 1)
  elseif string.sub(arg[i], 1, string.len(footerOption)) == footerOption then
    footerFileName = string.sub(arg[i], string.len(footerOption) + 1)
  else
    table.insert(inputFiles, arg[i])
  end
end

if headerFileName ~= "" then
  local headerFile = io.open(headerFileName, "r")
  if not headerFile then
    error("File: " .. headerFileName .. " does not exist.")
  end
  io.close(headerFile)

  for line in io.lines(headerFileName) do
    if string.find(line, sourceListPlaceHolder) then
      -- line containing "<source-files>" token is not copied to the output
      -- it serves as an indicator of where list of source files should be inserted
      sourcesLineIndex = #output
    else
      -- other lines in the header are copied to the output
      table.insert(output, line .. "\n")
    end
  end
end

table.insert(output, "ffi = require(\"ffi\")\nffi.cdef[[")

for _, fileName in ipairs(inputFiles) do

  local inputFile = io.open(fileName , "r")
  if not inputFile then
    error("File: " .. fileName .. " does not exist.")
  end
  io.close(inputFile)

  local isStartBlock = true
  local isEndBlock = false

  local isInHeader = false

  local currentBinding = ""

  for line in io.lines(fileName) do

    if line == headerBegin then
      isInHeader = true
    end

    if isInHeader then
      if string.find(line, ">> ") then
        if not table.contains(sourceFiles, line) then
          table.insert(sourceFiles, line)
        end
      end
    end

    if line == ffiBlockEnd then
      isEndBlock = true
    end

    if not isStartBlock and not isEndBlock then
      if not line or line == "" then
        if (currentBinding ~= "") then
          if not bindings[currentBinding] then
            table.insert(output, "\n")
            table.insert(output, currentBinding)
            bindings[currentBinding] = currentBinding
          end
        end
        currentBinding = ""
      else
        currentBinding = currentBinding .. "\n" .. line
      end
    end

    if line == ffiBlockBegin then
      isStartBlock = false
    end

    if line == headerEnd then
      isInHeader = false
    end

  end
end

if sourcesLineIndex ~= -1 then
  -- insert list of source files (each in new line)
  for i, sourceFileName in ipairs(sourceFiles) do
    table.insert(output, sourcesLineIndex + i, sourceFileName .. "\n")
  end
end

table.insert(output, "\n\n]]\n")

if footerFileName ~= "" then
  local footerFile = io.open(footerFileName, "r")
  if not footerFile then
    error("File: " .. footerFileName .. " does not exist.")
  end
  io.close(footerFile)

  for line in io.lines(footerFileName) do
    table.insert(output, line .. "\n")
  end
end

-- write output to a file
local outFile = io.open(outputFileName , "w")
if not outFile then
  error("Cannot write to file: " .. outputFileName)
end

for _, outputLine in ipairs(output) do
  outFile:write(outputLine)
end
io.close(outFile)
