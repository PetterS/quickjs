
test: install
	poetry run python -X dev -m unittest

install: build
	poetry run python setup.py develop

build: Makefile module.c upstream-quickjs/quickjs.c upstream-quickjs/quickjs.h
ifeq ($(shell uname | head -c5), MINGW)
	poetry run python setup.py build -c mingw32
else
	poetry run python setup.py build
endif

format:
	poetry run python -m yapf -i -r --style .style.yapf *.py
	clang-format-7 -i module.c

distribute-source: test
	rm -rf dist/
	poetry run python setup.py sdist

distribute-binary: test
	poetry run python setup.py bdist_wheel --skip-build

upload-test:
	poetry run python -m twine upload -r testpypi dist/*

upload:
	poetry run python -m twine upload dist/*

clean:
	rm -rf build/ dist/ 
	rm -f *.so
	rm -f *.pyd
