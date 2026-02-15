#! bash

if [[ $# != 1 ]] ; then
	echo "Wrong number of inputs. Require 1 argument, got $#"
	echo ""
	echo "This tool converts all .af files in a given folder to hex c-arrays"
	echo "Usage:"
	echo '  ./convert_to_hex.sh ${font-folder}'
	exit 1
fi

for file in $1/*.af ; do
	echo "Converting $file"
	cat $file | hexdump -v -e '/1 "0x%02x,"' > $file.carr
done

echo "Done converting all files"

