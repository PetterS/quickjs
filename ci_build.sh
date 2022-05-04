if [ $IS_64 == 1 ]
then
	PREFIX=""
else
	PREFIX="linux32"
fi

$PREFIX python$PYTHON_VERSION setup.py build_ext --inplace
$PREFIX python$PYTHON_VERSION -X dev -m unittest
