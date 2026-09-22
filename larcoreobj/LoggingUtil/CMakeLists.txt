cet_make_library(LIBRARY_NAME LoggingUtil
  SOURCE Logging.cxx
  LIBRARIES PUBLIC
  spdlog::spdlog
)

install_headers()
install_source()
