#!/bin/bash
IFS='' # prevent stripping leading spaces on file read

headerBegin="--[["
headerEnd="--]]"

ffiBlockBegin="ffi.cdef[["
ffiBlockEnd="]]"

sourceListPlaceHolder="<source-files>" 

outputOption="--output="
outputFileName="out.lua" # default output file name

headerOption="--header="
headerFileName=""
footerOption="--footer="
footerFileName=""

inputFiles=()       # list of files to combine
declare -A bindings # map of bindings
output=()           # list of lines to write to the output file
sourceFiles=()      # list of source files that given lua files are based on

sourcesLineIndex=-1

# this function determines whether the specified table contains the given element
function tableContains() {
  declare -a table=("${!1}")
  element="$2"
  for value in ${table[@]}; do
    if [ "$value" == "$element" ]; 
    then
      return 1
    fi
  done
  return 0
}

# get names of the lua files to combine (and if set, output, header and footer file names)
for arg in "$@"; do
  if [ "${arg:0:${#outputOption}}" == "$outputOption" ];
  then
    outputFileName=${arg:${#outputOption}}
  elif [ "${arg:0:${#headerOption}}" == "$headerOption" ];
  then
    headerFileName=${arg:${#headerOption}}
  elif [ "${arg:0:${#footerOption}}" == "$footerOption" ];
  then
    footerFileName=${arg:${#footerOption}}
  else
    inputFiles[${#inputFiles[@]}]=$arg
  fi
done

if [ "$headerFileName" != "" ];
then
  if [ ! -e $headerFileName ];  then
    echo "File: $headerFileName does not exist."
    exit
  fi
  while read -r line ; do
    if [[ "$line" == *"$sourceListPlaceHolder"* ]];
    then
      # line containing "<source-files>" token is not copied to the output
      # it serves as an indicator of where list of source files should be inserted
      sourcesLineIndex=${#output[@]}
    else
      # other lines in the header are copied to the output
      output[${#output[@]}]="$line\n"
    fi
  done < "$headerFileName"
  
fi

output[${#output[@]}]="ffi = require(\"ffi\")\nffi.cdef[["

for fileName in ${inputFiles[@]}; do
  if [ ! -e $fileName ];
  then
    echo "File: $fileName does not exist."
    exit
  fi
  isStartBlock=1
  isEndBlock=0
  isInHeader=0
  currentBinding=""
  while read line ; do
    if [ "$line" == "$headerBegin" ];
    then
      isInHeader=1
    fi
    if [ $isInHeader -eq 1 ];
    then
      if [[ "$line" == *">> "* ]];
      then
        tableContains sourceFiles[@] "$line"
        res=$?
        if [ $res -ne 1 ];
        then
          sourceFiles[${#sourceFiles[@]}]="$line";
        fi
      fi
    fi
    if [ "$line" == "$ffiBlockEnd" ];
    then
      isEndBlock=1
    fi
    if [ $isStartBlock -ne 1 ] && [ $isEndBlock -ne 1 ];
    then
      if [ -z "$line" ] || [ "$line" == "" ];
      then
        if [ "$currentBinding" != "" ];
        then
          if [ "${bindings[$currentBinding]}" == "" ];
          then
            output[${#output[@]}]="\n"
            output[${#output[@]}]=$currentBinding
            bindings["$currentBinding"]=$currentBinding
          fi
        fi
        currentBinding=""
      else
        currentBinding="$currentBinding\n$line"
      fi
    fi
    if [ "$line" == "$ffiBlockBegin" ];
    then
      isStartBlock=0
    fi
    if [ "$line" == "$headerEnd" ];
    then
      isInHeader=0
    fi
  done < $fileName  
done

if [ $sourcesLineIndex -ne -1 ];
then
  # insert list of source files (each in new line)
  sourcesLines="${output[$sourcesLineIndex-1]}"
  for sourceFileName in "${sourceFiles[@]}";
  do
    sourcesLines="$sourcesLines$sourceFileName\n"
  done
  output[$sourcesLineIndex-1]="$sourcesLines"
fi

output[${#output[@]}]="\n\n]]\n"

if [ ! $footerFileName == "" ];
then
  if [ ! -e $footerFileName ];  
    then
    echo "File: $footerFileName does not exist."
    exit
  fi
  while read -r line; 
  do
    output[${#output[@]}]="$line\n"
  done < $footerFileName
fi

# check whether we have permissions to write to a file
touch $outputFileName
if [ $? -ne 0 ] && [ ! -f $outputFileName ];
then
  echo "Cannot write to file: $outputFileName"
  exit
fi

# write output to a file
rm $outputFileName
for str in "${output[@]}"; do
  echo -e -n "$str" >> $outputFileName
done
