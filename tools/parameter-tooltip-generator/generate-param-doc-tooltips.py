"""
Grab all parameter files (markdown) and convert them to html files
"""
import os
import glob
import markdown


parameterDocsFolder = "./docs/Configuration/Parameter"
docsMainFolder = "./sd-card/html"
configPage = "edit_config_param.html"
referenceImagePage = "edit_reference.html"

htmlTooltipPrefix = """
    <div class="rst-content"><div class="tooltip"><img src="help.png" width="20px"><span class="tooltiptext">
"""


htmlTooltipSuffix = """
    </span></div></div>
"""


folders = sorted( filter( os.path.isdir, glob.glob(parameterDocsFolder + '/*') ) )


def generateHtmlTooltip(section, parameter, markdownFile):
    # print(section, parameter, markdownFile)

    with open(markdownFile, 'r') as markdownFileHandle:
        markdownFileContent = markdownFileHandle.read()

    # Convert markdown files to HTML file
    htmlTooltip = markdown.markdown(markdownFileContent, extensions=['admonition', 'tables'])

    # Make all links to be opened in a new page
    htmlTooltip = htmlTooltip.replace("a href", "a target=_blank href")

    # Replace relative documentation links with absolute ones pointing to the external documentation
    htmlTooltip = htmlTooltip.replace("href=\"../", "href=\"https://jomjol.github.io/AI-on-the-edge-device-docs/")

    # Update image paths and copy images to right folder
    if "../img/" in htmlTooltip:
        htmlTooltip = htmlTooltip.replace("../img/", "/")

    htmlTooltip = htmlTooltipPrefix + htmlTooltip + htmlTooltipSuffix

    ####### configPage: Add tooltips
    with open(docsMainFolder + "/" + configPage, 'r') as configPageHandle:
        configPageContent = configPageHandle.read()

    # print("replacing $TOOLTIP_" + section + "_" + parameter + " with the tooltip content...")
    configPageContent = configPageContent.replace("<th hidden>$TOOLTIP_" + section + "_" + parameter + "</th>",
                                                    "<th style=\"font-weight: unset;\">" + htmlTooltip + "</th>")
    configPageContent = configPageContent.replace("<td style=\"visibility:hidden\">$TOOLTIP_" + section + "_" + parameter + "</td>",
                                                    "<td>" + htmlTooltip + "</td>")

    with open(docsMainFolder + "/" + configPage, 'w') as configPageHandle:
        configPageHandle.write(configPageContent)

    ####### referenceImagePage: Add tooltips
    with open(docsMainFolder + "/" + referenceImagePage, 'r') as referenceImagePageHandle:
        referenceImagePageContent = referenceImagePageHandle.read()

    # print("replacing $TOOLTIP_" + section + "_" + parameter + " with the tooltip content...")
    referenceImagePageContent = referenceImagePageContent.replace("<td style=\"visibility:hidden\">$TOOLTIP_" + section + "_" + parameter + "</td>",
                                                                  "<td>" + htmlTooltip + "</td>")

    with open(docsMainFolder + "/" + referenceImagePage, 'w') as referenceImagePageHandle:
        referenceImagePageHandle.write(referenceImagePageContent)


print("Generating Tooltips...")

"""
Generate a HTML tooltip for each markdown page
"""
for folder in folders:
    folder = folder.split("/")[-1]

    files = sorted(filter(os.path.isfile, glob.glob(parameterDocsFolder + "/" + folder + '/*')))
    for file in files:
        if not ".md" in file: # Skip non-markdown files
            continue

        parameter = file.split("/")[-1].replace(".md", "")
        parameter = parameter.replace("<", "").replace(">", "")
        generateHtmlTooltip(folder.lower(), parameter.lower(), file)

"""
Copy images to main folder
"""
os.system("cp " + parameterDocsFolder + "/img/* " + docsMainFolder + "/")
