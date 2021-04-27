## ----setup options, include=FALSE---------------------------------------------
knitr::opts_chunk$set(error = TRUE, eval = FALSE)

# Get environment variables describing what to evaluate
run <- tolower(Sys.getenv("RUN_DEVDOCS", "false")) == "true"
macos <- tolower(Sys.getenv("DEVDOCS_MACOS", "false")) == "true"
ubuntu <- tolower(Sys.getenv("DEVDOCS_UBUNTU", "false")) == "true"
sys_install <- tolower(Sys.getenv("DEVDOCS_SYSTEM_INSTALL", "false")) == "true"

# Update the source knit_hook to save the chunk (if it is marked to be saved)
knit_hooks_source <- knitr::knit_hooks$get("source")
knitr::knit_hooks$set(source = function(x, options) {
  # Extra paranoia about when this will write the chunks to the script, we will
  # only save when:
  #   * CI is true
  #   * RUN_DEVDOCS is true
  #   * options$save is TRUE (and a check that not NULL won't crash it)
  if (as.logical(Sys.getenv("CI", FALSE)) && run && !is.null(options$save) && options$save)
    cat(x, file = "script.sh", append = TRUE, sep = "\n")
  # but hide the blocks we want hidden:
  if (!is.null(options$hide) && options$hide) {
    return(NULL)
  }
  knit_hooks_source(x, options)
})

