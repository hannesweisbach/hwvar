if [ -r ${SOFTENV_ALIASES} ] ; then
  source ${SOFTENV_ALIASES}
  soft add +cmake
else
  echo "Unable to load SoftEnv system."
fi

NODE=$1
COMPILER=$2

if [ -z "$NODE" ] ; then
  echo "Node argument missing. Choose 'login' or 'cnk'."
  exit
fi

if [ -z "$COMPILER" ] ; then
  echo "Compiler argument missing. Choose 'gcc' or 'xlc'."
  exit
fi

echo "Building for $NODE/$COMPILER"

BUILDDIR=build-$NODE-$COMPILER
LOG=make-$NODE-$COMPILER.log

rm $LOG

rm -r $BUILDDIR
mkdir $BUILDDIR
cd $BUILDDIR

if [ x"$2" = "xxlc" ] ; then

  echo "Using xlc"

  if [ $NODE = "cnk" ] ; then
    soft add +mpiwrapper-xl
  else
    export CC=xlc_r
    export CXX=xlc++_r
  fi

  export CFLAGS=" -O5 "
  export CXXFLAGS=" -O5 "
  #export CFLAGS+=" -qlist -qlistopt -qlistfmt -qattr  -qxref "
  #export CFLAGS+=" -g9"

else

  echo "Using gcc"
  
  if [ $NODE = "cnk" ] ; then
    soft add +mpiwrapper-gcc
  else
    export CC=gcc
    export CXX=g++
  fi
  
  export CFLAGS=" -O3 "
  export CXXFLAGS=" -O3 "
  #export CFLAGS+=" -g"
  
fi

if [ $NODE = "cnk" ] ; then
  export CC=mpicc
  export CXX=mpicxx
  export CMAKENODE="-DCMAKE_SYSTEM_NAME=BlueGeneQ-static"
fi

if [ x"$COMPILER" = "xxlc" ] ; then
  $CC  -qversion=verbose >> ../$LOG 
  $CXX -qversion=verbose >> ../$LOG
else
  $CC  --version >> ../$LOG
  $CXX --version >> ../$LOG
fi

export HWLOC_DIR=/projects/HWPerfVar/hwloc/install/hwloc-$NODE-$COMPILER

cmake --version > ../$LOG

cmake $CMAKENODE ../hwvar 2>&1 | tee -a ../$LOG
make VERBOSE=1 2>&1 |tee -a ../$LOG

cd ..
