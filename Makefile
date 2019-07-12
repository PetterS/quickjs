
test: install
	python3 -m unittest

install: build
	python3 setup.py install --user

build: Makefile module.c
	python3 setup.py build

format:
	python3 -m yapf -i -r --style .style.yapf  .
	clang-format-7 -i module.c
