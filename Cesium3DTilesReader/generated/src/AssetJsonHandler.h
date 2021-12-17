// This file was generated by generate-classes.
// DO NOT EDIT THIS FILE!
#pragma once

#include <Cesium3DTiles/Asset.h>
#include <CesiumJsonReader/ExtensibleObjectJsonHandler.h>
#include <CesiumJsonReader/StringJsonHandler.h>

namespace CesiumJsonReader {
class ExtensionReaderContext;
}

namespace Cesium3DTilesReader {
class AssetJsonHandler : public CesiumJsonReader::ExtensibleObjectJsonHandler {
public:
  using ValueType = Cesium3DTiles::Asset;

  AssetJsonHandler(
      const CesiumJsonReader::ExtensionReaderContext& context) noexcept;
  void reset(IJsonHandler* pParentHandler, Cesium3DTiles::Asset* pObject);

  virtual IJsonHandler* readObjectKey(const std::string_view& str) override;

protected:
  IJsonHandler* readObjectKeyAsset(
      const std::string& objectType,
      const std::string_view& str,
      Cesium3DTiles::Asset& o);

private:
  Cesium3DTiles::Asset* _pObject = nullptr;
  CesiumJsonReader::StringJsonHandler _version;
  CesiumJsonReader::StringJsonHandler _tilesetVersion;
};
} // namespace Cesium3DTilesReader