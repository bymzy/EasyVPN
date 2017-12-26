XX=g++
CXXFLAGS=-g
TARGET=udptun
LIB=
MARCOS=-D_LARGEFILE64_SOURCE -D _LARGEFILE_SOURCE -D_LARGE_FILES -D _FILE_OFFSET_BITS=64

SOURCEDIR=src
OBJDIR=objs
BINDIR=bin

SOURCES=${wildcard ${SOURCEDIR}/*.cpp}
OBJS=${patsubst ${SOURCEDIR}/%.cpp, ${OBJDIR}/%.o, ${SOURCES}}
OBJDEPS=${OBJS:.o=.d}

${BINDIR}/${TARGET}:${OBJS} ${OBJDEPS}
	${shell mkdir -p ${BINDIR}}
	${XX} ${LIB} -o ${BINDIR}/${TARGET} ${OBJS}

${OBJDIR}/%.o:${SOURCEDIR}/%.cpp
	${shell mkdir -p ${OBJDIR}}
	${XX} -c $< -o $@ ${MARCOS} ${CXXFLAGS}

${OBJDIR}/%.d:${SOURCEDIR}/%.cpp
	${shell mkdir -p ${OBJDIR}}
	@${XX} -MM -c $< | sed '/\.o/ s/^/${OBJDIR}\//' >$@

-include ${OBJDEPS}

output:
	echo ${OBJS}
	echo ${OBJDEPS}

help:
	@echo "... clean (rm objs and bin)"
	@echo "... info (show info of this program)"
.PHONY : help

info:
	@echo "program writed by mzy. Using libcurl to download file from Internet"
	@echo "sources are all in src dir, objs are all in objs dirs"
	@echo "binary file is in bin dir"
.PHONY : info

clean:
	@rm -rf ${OBJS} ${BINDIR}/${TARGET} ${OBJDEPS}
.PHONY : clean
