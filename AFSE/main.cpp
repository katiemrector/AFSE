#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <ShlObj.h>

const int POS_OFFSET = 0x752;
const int YAW_TEXT_OFFSET = 0x7E6;
const int YAW_OFFSET = 0x85E;
char errmsg[30];

struct vec3_64 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

double degreesToRadians(double angleDegrees) {
    return angleDegrees * 3.14159265 / 180;
}

// windows stinks
std::filesystem::path getSaveGamesFolderPath(REFKNOWNFOLDERID folderID) {
    wchar_t* filePath;
    if (SHGetKnownFolderPath(folderID, 0, NULL, &filePath) != S_OK) {
        std::cerr << "Failed to resolve LocalAppData path\n";
        return std::filesystem::path();
    }
    
    std::wstringstream wss;
    wss << filePath << L"\\AbioticFactor\\Saved\\SaveGames"; // stringstream fuckery to get the appdata path for abf
    std::wstring pathStr = wss.str();
    CoTaskMemFree(static_cast<void*>(filePath));
    return std::filesystem::path(pathStr);
}

vec3_64 getPlayerPositionFromFile(std::filesystem::path saveFilePath) {
    vec3_64 posVector = { 0.0,0.0,0.0 };
    std::ifstream saveFile(saveFilePath, std::ios::in | std::ios::binary); // new ifstream for fresh data every time
    if (saveFile) {
        saveFile.seekg(POS_OFFSET, std::ios::beg);
        saveFile.read(reinterpret_cast<char*>(&posVector.x), sizeof(double)); // read x pos
        saveFile.read(reinterpret_cast<char*>(&posVector.y), sizeof(double)); // and y
        saveFile.read(reinterpret_cast<char*>(&posVector.z), sizeof(double)); // and z
        saveFile.close();
        return posVector;
    }
    strerror_s(errmsg, 30, errno);
    std::cerr << "Failed to open file: " << errmsg << '\n';
    return posVector;
}

double getPlayerYawFromFile(std::filesystem::path saveFilePath) {
    double yawValue = 0.0;
    char yawText[0x14];
    std::ifstream saveFile(saveFilePath, std::ios::in | std::ios::binary); // new ifstream for fresh data every time
    if (saveFile) {
        saveFile.seekg(YAW_TEXT_OFFSET, std::ios::beg);
        saveFile.get(yawText, 0x14);
        if (std::string(yawText) == "LastControlRotation") {
            saveFile.seekg(YAW_OFFSET, std::ios::beg);
            saveFile.read(reinterpret_cast<char*>(&yawValue), sizeof(double)); // read yaw
        }
        saveFile.close();
        return yawValue;
    }
    strerror_s(errmsg, 30, errno);
    std::cerr << "Failed to open file: " << errmsg << '\n';
    return INFINITY;
}

int writePlayerPositionToFile(std::filesystem::path saveFilePath, vec3_64 posVector) {
    double d = 0.0;
    std::ofstream saveFile(saveFilePath, std::ios::binary | std::ios::out | std::ios::in); // why is ios::in required here lmfao
    if (saveFile) {
        saveFile.seekp(POS_OFFSET, std::ios::beg);
        saveFile.write(reinterpret_cast<char*>(&posVector.x), sizeof(d)); // read x pos
        saveFile.write(reinterpret_cast<char*>(&posVector.y), sizeof(d)); // and y
        saveFile.write(reinterpret_cast<char*>(&posVector.z), sizeof(d));
        saveFile.close();
        return 1;
    }
    strerror_s(errmsg, 30, errno);
    std::cerr << "Failed to open file: " << errmsg << '\n';
    return 0;
}

int getWorldNamesFromPath(std::filesystem::path worldsFolderPath, std::filesystem::path (&worldNamesArray)[128]) {
    int count = 0;
    for (int i = 0; i < 128; i++)
        worldNamesArray[i] = ""; // clear array entries

    for (auto const& dir_entry : std::filesystem::directory_iterator{ worldsFolderPath })
        if (dir_entry.is_directory()) {
            worldNamesArray[count] = dir_entry.path();
            count++;
        }
    return count;
}

int main() {
    std::filesystem::path savesFolderPath = getSaveGamesFolderPath(FOLDERID_LocalAppData);
    if (!std::filesystem::exists(savesFolderPath)) {
        std::cerr << "No save data found\n";
        return 2;
    }
    for (auto const& dir_entry : std::filesystem::directory_iterator{ savesFolderPath })
        if (dir_entry.is_directory() && dir_entry.path().stem() != "PlaytestSaves") {
            savesFolderPath = dir_entry.path();
            break;
        }

    savesFolderPath /= "Worlds";
    if (!std::filesystem::exists(savesFolderPath)) { // we do little a error checking
        std::cerr << "\"Worlds\" folder not found\n";
        return 3;
    }
    
    bool isWorldChosen = false, isFinishedUsingProgram = false, isDoneEditing = false, isSavedPos = false;
    int numChoices, choice = -1;
    std::string inputBuffer;
    std::string::size_type size;
    std::filesystem::path worldFolders[128] = { "" }, currWorldPath = "", saveFilePath = "";
    vec3_64 playerPos = { 0.0 }, savedPos = { 0.0 };
    double playerYaw = 0.0, moveDistance = 0.0;

    // now we need the user to choose a world to edit
    while (!isFinishedUsingProgram) {
        system("cls");
        std::cout << "Abiotic Factor Save Editor v1.1.0 by k80may\n\n";
        isWorldChosen = false, isDoneEditing = false;
        while (!isWorldChosen) {
            numChoices = getWorldNamesFromPath(savesFolderPath, worldFolders); // get world count and paths
            for (int i = 0; i < 128; i++) {
                if (worldFolders[i].string() == "")
                    break;
                std::cout << i + 1 << ". " << worldFolders[i].stem().string() << '\n'; // list world names in human friendly list
            }

            std::cout << "\nChoose a world file to edit ('r' to refresh list, 'x' to exit): \n>> ";
            std::getline(std::cin, inputBuffer);
            system("cls");
            if (inputBuffer == "r")
                continue;
            else if (inputBuffer == "x")
                return 0;
            try {
                choice = std::stoi(inputBuffer, &size);
            }
            catch (...) {}
            if (choice < 1 || choice > numChoices) {
                std::cerr << "Invalid choice.\n\n";
            }
            else {
                if (std::filesystem::exists(worldFolders[choice - 1])) {
                    currWorldPath = worldFolders[choice - 1];
                    isWorldChosen = true;
                }
                else {
                    std::cerr << "Path not found. Was world deleted after last refresh?\n\n";
                }
            }
        }

        currWorldPath /= "PlayerData";

        for (auto const& dir_entry : std::filesystem::directory_iterator{ currWorldPath })
            if (dir_entry.is_regular_file() && dir_entry.path().extension() == ".sav" && dir_entry.path().stem().string().find(" ") == std::string::npos && dir_entry.path().stem().string()._Starts_with("Player")) {
                saveFilePath = dir_entry.path();
                break;
            }

        if (!std::filesystem::exists(saveFilePath)) {
            std::cerr << "Error reading save file\n";
            return 4;
        }
        
        isSavedPos = false;
        playerPos = { 0.0 }, savedPos = { 0.0 };
        playerYaw = 0.0;
        while (!isDoneEditing) {
            std::cout << "--EXIT WORLD BEFORE EDITING COORDS--\n\nCurrent world: " << currWorldPath.parent_path().stem().string() << "\n\n";
            std::cout << "1. Restore saved position ";
            if (!isSavedPos)
                std::cout << "(none)\n";
            else
                std::cout << std::fixed << std::setprecision(3) << '(' << savedPos.x << ", " << savedPos.y << ", " << savedPos.z << ')' << '\n';
            std::cout << "2. Set current position as saved position\n3. Move current position forward (in facing direction)\n4. Edit current position\n5. Edit saved position\n0. Change current save file\n\n";
            std::cout << "Choose an option ('x' to exit): \n>> ";
            std::getline(std::cin, inputBuffer);
            system("cls");
            char coordNames[3] = { 'x', 'z', 'y' };
            double tempPos[5] = { 0.0 }, originalPos[3] = { 0.0 };
            size = 0;
            if (inputBuffer == "1") {
                if (!isSavedPos) {
                    std::cout << "No position saved. Continue? ('y' or 'n')\n";
                    std::getline(std::cin, inputBuffer);
                    if (!inputBuffer._Starts_with("y")) {
                        if (inputBuffer._Starts_with("n"))
                            std::cout << "Returning to menu...\n";
                        else
                            std::cerr << "Invalid input. Returning to menu...\n";
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                        system("cls");
                        continue;
                    }
                }
                if (writePlayerPositionToFile(saveFilePath, savedPos))
                    std::cout << "Position set to (" << std::fixed << std::setprecision(3) << savedPos.x << ", " << savedPos.y << ", " << savedPos.z << ")\n\n";
                else
                    std::cerr << "Error writing position to file.\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            else if (inputBuffer == "2") {
                savedPos = getPlayerPositionFromFile(saveFilePath);
                isSavedPos = true;
                std::cout << "Saved position set to (" << std::fixed << std::setprecision(3) << savedPos.x << ", " << savedPos.y << ", " << savedPos.z << ")\n\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            else if (inputBuffer == "3") {
                playerYaw = getPlayerYawFromFile(saveFilePath);
                if (playerYaw == INFINITY) {
                    std::cout << "Error reading player yaw.\n";
                    continue;
                }
                playerPos = getPlayerPositionFromFile(saveFilePath);
                if (playerPos.x == 0.0 && playerPos.y == 0.0 && playerPos.z == 0.0) {
                    std::cerr << "Error reading player position.\n";
                    continue;
                }
                std::cout << "Enter distance to move player:\n>> ";
                std::getline(std::cin, inputBuffer);
                system("cls");
                try {
                    moveDistance = std::stod(inputBuffer, &size);
                }
                catch (...) {
                    std::cerr << "Invalid coordinate." << '\n';
                }
                playerPos.x = (sin(degreesToRadians(90.0 - playerYaw)) * moveDistance) + playerPos.x;
                playerPos.y = (cos(degreesToRadians(90.0 - playerYaw)) * moveDistance) + playerPos.y;
                if (writePlayerPositionToFile(saveFilePath, playerPos))
                    std::cout << "Position set to (" << std::fixed << std::setprecision(3) << playerPos.x << ", " << playerPos.y << ", " << playerPos.z << ")\n\n";
                else
                    std::cerr << "Error writing position to file.\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            else if (inputBuffer == "4") {
                playerPos = getPlayerPositionFromFile(saveFilePath);
                if (playerPos.x == 0.0 && playerPos.y == 0.0 && playerPos.z == 0.0) {
                    std::cerr << "Error reading player position.\n";
                    continue;
                }
                originalPos[0] = playerPos.x;
                originalPos[1] = playerPos.y;
                originalPos[2] = playerPos.z;
                for (int i = 0; i < 3; i++) {
                    std::cout << "Enter coordinates (leave blank for no change):\n\nOriginal " << coordNames[i] << ": " << std::fixed << std::setprecision(3) << originalPos[i] << "\nNew " << coordNames[i] << ": ";
                    std::getline(std::cin, inputBuffer);
                    system("cls");
                    if (inputBuffer.empty())
                        tempPos[i] = originalPos[i];
                    else {
                        try {
                            tempPos[i] = std::stod(inputBuffer, &size);
                        }
                        catch (...) {
                            std::cerr << "Invalid coordinate." << '\n';
                            i--;
                        }
                    }
                }
                playerPos = { tempPos[0], tempPos[1], tempPos[2] };
                if (writePlayerPositionToFile(saveFilePath, playerPos))
                    std::cout << "Position set to (" << std::fixed << std::setprecision(3) << playerPos.x << ", " << playerPos.y << ", " << playerPos.z << ")\n\n";
                else
                    std::cerr << "Error writing position to file.\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            else if (inputBuffer == "5") {
                originalPos[0] = savedPos.x;
                originalPos[1] = savedPos.y;
                originalPos[2] = savedPos.z;
                for (int i = 0; i < 3; i++) {
                    std::cout << "Enter coordinates (leave blank for no change):\n\nOriginal " << coordNames[i] << ": " << std::fixed << std::setprecision(3) << originalPos[i] << "\nNew " << coordNames[i] << ": ";
                    std::getline(std::cin, inputBuffer);
                    system("cls");
                    if (inputBuffer.empty())
                        tempPos[i] = originalPos[i];
                    else {
                        try {
                            tempPos[i] = std::stod(inputBuffer, &size);
                        }
                        catch (...) {
                            std::cerr << "Invalid coordinate." << '\n';
                            i--;
                        }
                    }
                }
                savedPos = { tempPos[0], tempPos[1], tempPos[2] };
                isSavedPos = true;
                std::cout << "Saved position set to (" << std::fixed << std::setprecision(3) << savedPos.x << ", " << savedPos.y << ", " << savedPos.z << ")\n\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            else if (inputBuffer == "0")
                isDoneEditing = true;
            else if (inputBuffer == "x")
                return 0;
            else
                std::cerr << "Invalid command.\n\n";
        }
    }
    return 0;
}